#include "rm2027/video/video_receiver.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <algorithm>
#include <cstring>

#include "rm2027/common/log.h"
#include "rm2027/video/udp_header.h"

namespace rm2027 {

namespace {
int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

VideoReceiver::VideoReceiver(VideoReceiverOptions opt)
    : opt_(std::move(opt)), decoder_(std::make_unique<VideoDecoder>()) {}

VideoReceiver::~VideoReceiver() { stop(); }

void VideoReceiver::setOptions(const VideoReceiverOptions &opt) {
    if (running_.load()) {
        RM_WARN("video-rx", "运行中改配置被忽略（请先 stop）");
        return;
    }
    opt_ = opt;
}

void VideoReceiver::setFrameHandler(FrameHandler h) { handler_ = std::move(h); }

void VideoReceiver::setCodecMode(VideoCodecMode mode) {
    const bool changed = codecMode_.exchange(static_cast<int>(mode)) != static_cast<int>(mode);
    if (!changed) return;
    RM_INFO("video-rx", "解码格式模式 -> " << videoCodecModeName(mode)
                                           << (mode == VideoCodecMode::Auto ? "（探针自动选择）"
                                                                            : "（手动固定）"));
    if (mode == VideoCodecMode::Auto) return;
    // 手动固定：立刻切到指定解码器
    std::lock_guard<std::mutex> lk(decoderMutex_);
    const VideoCodec want = videoCodecModeCodec(mode);
    if (decoder_->isOpen() && decoder_->codec() == want) return;
    std::string err;
    if (!decoder_->open(want, &err)) {
        RM_WARN("video-rx", "切换到 " << videoCodecName(want) << " 失败: " << err);
        return;
    }
    codec_.store(static_cast<int>(want));
    lastAuSeen_ = false;
    needKeyframe_ = true;
}

VideoCodecMode VideoReceiver::codecMode() const {
    return static_cast<VideoCodecMode>(codecMode_.load());
}

bool VideoReceiver::start(std::string *error) {
    if (running_.load()) return true;

    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
        if (error) *error = std::string("socket() 失败: ") + std::strerror(errno);
        return false;
    }
    int one = 1;
    ::setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    // 1080p60 突发很大，接收缓冲开大，尽量不丢在本地
    int rcvbuf = 16 * 1024 * 1024;
    ::setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(opt_.port);
    if (opt_.bindHost.empty() || opt_.bindHost == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, opt_.bindHost.c_str(), &addr.sin_addr) != 1) {
        if (error) *error = "无法解析绑定地址: " + opt_.bindHost;
        ::close(socket_);
        socket_ = -1;
        return false;
    }
    if (::bind(socket_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        if (error) *error = std::string("bind(") + opt_.bindHost + ":" +
                            std::to_string(opt_.port) + ") 失败: " + std::strerror(errno);
        ::close(socket_);
        socket_ = -1;
        return false;
    }

    pending_.clear();
    lastAuSeen_ = false;
    needKeyframe_ = true;
    haveNext_ = false;
    haveLastCompleted_ = false;
    {
        std::lock_guard<std::mutex> lk(auMutex_);
        auQueue_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(slotMutex_);
        slotSeq_ = 0;
        slotData_.clear();
        slotInfo_ = VideoFrame{};
    }

    codecMode_.store(static_cast<int>(opt_.codecMode));
    running_.store(true);
    draining_ = false;
    rateBaseMs_.store(nowMs());
    rxThread_ = std::thread([this] { rxLoop(); });
    decodeThread_ = std::thread([this] { decodeLoop(); });

    RM_INFO("video-rx", "图传接收端已启动: udp://" << opt_.bindHost << ":" << opt_.port
                                                   << " (包头 " << udpHeaderEndianName() << ")");
    return true;
}

void VideoReceiver::stop() {
    if (!running_.exchange(false)) return;
    draining_ = true;   // 让解码线程把缓存帧冲刷出来
    auCv_.notify_all();
    if (socket_ >= 0) {
        ::shutdown(socket_, SHUT_RDWR);
    }
    if (rxThread_.joinable()) rxThread_.join();
    if (decodeThread_.joinable()) decodeThread_.join();
    if (socket_ >= 0) {
        ::close(socket_);
        socket_ = -1;
    }
    RM_INFO("video-rx", "图传接收端已停止: 完整帧 " << framesComplete_.load() << " / 不完整 "
                                                    << framesIncomplete_.load() << " / 解码 "
                                                    << framesDecoded_.load() << " / 解码错误 "
                                                    << decodeErrors_.load());
}

// --------------------------------------------------------------------- 组帧
void VideoReceiver::sweepStale(int64_t now) {
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (now - it->second.firstSeenMs > opt_.frameTimeoutMs) {
            framesIncomplete_.fetch_add(1);
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
    // 同时在组的帧太多 -> 丢最旧的
    while (pending_.size() > opt_.maxPendingFrames) {
        auto it = pending_.begin();
        framesAbandoned_.fetch_add(1);
        pending_.erase(it);
    }
}

void VideoReceiver::enqueueAu(uint16_t frameNumber, std::string au) {
    bool overflow = false;
    {
        std::lock_guard<std::mutex> lk(auMutex_);
        if (auQueue_.size() >= opt_.auQueueDepth) {
            // 队满：整队丢弃并等下一个关键帧（丢 delta 帧会破坏参考链）
            auQueue_.clear();
            overflow = true;
        }
        auQueue_.emplace_back(frameNumber, std::move(au));
        ++auSeq_;
    }
    if (overflow) {
        std::lock_guard<std::mutex> lk(decoderMutex_);
        if (decoder_) {
            decoder_->close(); // 下次遇到 IDR 会自动重开
            decoderResets_.fetch_add(1);
            needKeyframe_ = true;
            lastAuSeen_ = false;
        }
        keyframeWaits_.fetch_add(1);
        waitingKeyframe_.store(true);
    }
    auCv_.notify_one();
}

void VideoReceiver::handlePacket(const uint8_t *data, std::size_t len) {
    if (len < kUdpVideoHeaderSize) {
        badPackets_.fetch_add(1);
        return;
    }
    const UdpVideoHeader h = readUdpVideoHeader(data);
    const std::size_t payloadLen = len - kUdpVideoHeaderSize;
    packetsReceived_.fetch_add(1);
    bytesReceived_.fetch_add(len);

    if (h.frameTotalBytes == 0 || h.frameTotalBytes > opt_.maxFrameBytes) {
        badPackets_.fetch_add(1);
        return;
    }

    const int64_t now = nowMs();
    sweepStale(now);

    auto it = pending_.find(h.frameNumber);
    if (it == pending_.end()) {
        PendingFrame pf;
        pf.expected = h.frameTotalBytes;
        pf.firstSeenMs = now;
        it = pending_.emplace(h.frameNumber, std::move(pf)).first;
        if (!haveNext_) {
            haveNext_ = true;
            nextFrame_ = h.frameNumber;
            haveLastCompleted_ = false;
        }
    }
    PendingFrame &f = it->second;
    if (f.expected != h.frameTotalBytes) {
        badPackets_.fetch_add(1); // 同一帧号出现不同长度声明
        return;
    }
    auto ins = f.fragments.emplace(h.fragmentIndex,
                                   std::string(reinterpret_cast<const char *>(data) + kUdpVideoHeaderSize,
                                               payloadLen));
    if (!ins.second) {
        duplicateFragments_.fetch_add(1);
        return;
    }
    f.got += payloadLen;

    if (f.got == f.expected) {
        // 按 fragmentIndex 顺序拼接（不假设分片大小）
        std::string au;
        au.reserve(f.expected);
        for (const auto &kv : f.fragments) au += kv.second;

        framesComplete_.fetch_add(1);
        if (haveLastCompleted_ &&
            static_cast<uint16_t>(h.frameNumber) != static_cast<uint16_t>(lastCompletedFrame_ + 1)) {
            outOfOrderFrames_.fetch_add(1);
        }
        lastCompletedFrame_ = h.frameNumber;
        haveLastCompleted_ = true;
        const uint16_t doneFrame = h.frameNumber;
        pending_.erase(it);
        enqueueAu(doneFrame, std::move(au));
    }
}

void VideoReceiver::rxLoop() {
    std::vector<char> buf(65536);
    while (running_.load()) {
        const ssize_t n = ::recv(socket_, buf.data(), buf.size(), 0);
        if (n > 0) {
            handlePacket(reinterpret_cast<const uint8_t *>(buf.data()), static_cast<std::size_t>(n));
        } else if (n < 0 && (errno == EINTR)) {
            continue;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        } else {
            if (running_.load()) RM_WARN("video-rx", "recv 失败: " << std::strerror(errno));
            break;
        }
    }
}

// --------------------------------------------------------------------- 解码
void VideoReceiver::storeFrame(const std::vector<uint8_t> &bgr, int w, int h, uint64_t auNo,
                               bool key) {
    std::lock_guard<std::mutex> lk(slotMutex_);
    // 只有「上一帧还没被取走就被新帧覆盖」才算真正被丢弃（预览语义下的正常丢帧）
    if (!slotConsumed_) droppedBySlot_.fetch_add(1);
    slotConsumed_ = false;
    slotData_ = bgr;
    slotInfo_.data = slotData_.data();
    slotInfo_.width = w;
    slotInfo_.height = h;
    slotInfo_.stride = w * 3;
    slotInfo_.frameNumber = auNo;
    slotInfo_.keyframe = key;
    ++slotSeq_;
    width_.store(w);
    height_.store(h);
}

void VideoReceiver::decodeLoop() {
    while (running_.load()) {
        std::string au;
        uint16_t auFrameNo = 0;
        {
            std::unique_lock<std::mutex> lk(auMutex_);
            auCv_.wait_for(lk, std::chrono::milliseconds(100),
                           [this] { return !auQueue_.empty() || !running_.load(); });
            if (!running_.load()) break;
            if (auQueue_.empty()) {
                // 队列空：若正在停止，冲刷解码器把 B 帧重排序缓存的尾部帧取出来
                if (!draining_) continue;
            }
            if (auQueue_.empty()) {
                std::lock_guard<std::mutex> dl(decoderMutex_);
                if (!decoder_->isOpen()) break;
                std::string ferr;
                auto fs = decoder_->flush(&ferr);
                if (fs != VideoDecoder::Status::FrameReady) break;
                framesDecoded_.fetch_add(1);
                const int fw = decoder_->frameWidth(), fh = decoder_->frameHeight();
                std::vector<uint8_t> copy(decoder_->frameData(),
                                          decoder_->frameData() +
                                              static_cast<std::size_t>(decoder_->frameStride()) * fh);
                storeFrame(copy, fw, fh, 0, decoder_->frameIsKeyframe());
                continue;
            }
            auFrameNo = auQueue_.front().first;
            au = std::move(auQueue_.front().second);
            auQueue_.pop_front();
        }
        const uint64_t auNo = auFrameNo;

        // ---- 帧号连续性检查 ----
        if (lastAuSeen_) {
            const uint16_t expectedNext = static_cast<uint16_t>(lastAuFrame_ + 1);
            if (auFrameNo != expectedNext) {
                // 中间缺了帧 -> 参考链断裂。此时继续喂 P 帧会用过期参考解出**错误画面**，
                // 所以立刻进入「等关键帧」状态，直到下一个 IDR/CRA 才恢复。
                frameGaps_.fetch_add(1);
                needKeyframe_ = true;
            }
        }
        lastAuSeen_ = true;
        lastAuFrame_ = auFrameNo;

        const bool isKey =
            VideoDecoder::isKeyframeAu(reinterpret_cast<const uint8_t *>(au.data()), au.size());
        // 注意：这里【不】因为参考链断裂就跳过增量帧。
        // 数字图传的通行做法是照常解码、照常显示，让解码器的错误隐藏去补，
        // 操作手需要的是运动趋势，冻结画面等于让他失明。
        if (isKey) needKeyframe_ = false;

        // 格式选择：手动固定则完全跳过探测，否则用探针
        const VideoCodecMode cmode = static_cast<VideoCodecMode>(codecMode_.load());
        const VideoCodec detected =
            (cmode == VideoCodecMode::Auto)
                ? VideoDecoder::detectCodec(reinterpret_cast<const uint8_t *>(au.data()), au.size())
                : videoCodecModeCodec(cmode);

        std::lock_guard<std::mutex> lk(decoderMutex_);
        // 只有「探测到且与当前不同」才切换；探测不出来就保持当前解码器（粘性）
        const VideoCodec want =
            (detected != VideoCodec::Unknown) ? detected : decoder_->codec();
        // 手动模式下若与当前不符，切过去
        if (!decoder_->isOpen() || (want != VideoCodec::Unknown && decoder_->codec() != want)) {
            std::string err;
            const VideoCodec open = (want == VideoCodec::Unknown) ? VideoCodec::H265 : want;
            if (!decoder_->open(open, &err)) {
                decodeErrors_.fetch_add(1);
                RM_WARN("video-rx", "打开解码器失败: " << err);
                continue;
            }
            codec_.store(static_cast<int>(decoder_->codec()));
        }
        waitingKeyframe_.store(decoder_->needsKeyframe());

        std::string err;
        const auto st = decoder_->decode(reinterpret_cast<const uint8_t *>(au.data()), au.size(),
                                         &err);
        if (st == VideoDecoder::Status::Error) {
            decodeErrors_.fetch_add(1);
            if (decodeErrors_.load() <= 5) {
                RM_WARN("video-rx", "解码失败(帧号 " << auFrameNo << ", " << au.size()
                                                     << " 字节): " << err);
            }
            // 不销毁解码器（重建代价高），打标记等下一个 IDR 自然重同步
            keyframeWaits_.fetch_add(1);
            waitingKeyframe_.store(true);
            continue;
        }
        if (st == VideoDecoder::Status::Buffering) {
            keyframeWaits_.fetch_add(1);
            waitingKeyframe_.store(true);
            continue;
        }

        waitingKeyframe_.store(false);
        framesDecoded_.fetch_add(1);
        const int w = decoder_->frameWidth();
        const int h = decoder_->frameHeight();
        std::vector<uint8_t> copy(decoder_->frameData(),
                                  decoder_->frameData() +
                                      static_cast<std::size_t>(decoder_->frameStride()) * h);
        storeFrame(copy, w, h, auNo, decoder_->frameIsKeyframe());

        if (handler_) {
            VideoFrame vf;
            vf.data = copy.data();
            vf.width = w;
            vf.height = h;
            vf.stride = decoder_->frameStride();
            vf.frameNumber = auNo;
            vf.keyframe = decoder_->frameIsKeyframe();
            handler_(vf);
        }
    }
}

// --------------------------------------------------------------------- 取帧
bool VideoReceiver::getLatestFrame(uint64_t *sinceSeq, std::vector<uint8_t> *out, VideoFrame *info) {
    std::lock_guard<std::mutex> lk(slotMutex_);
    if (!sinceSeq || !out) return false;
    if (slotSeq_ == 0 || slotSeq_ == *sinceSeq) return false;
    out->assign(slotData_.begin(), slotData_.end());
    if (info) {
        *info = slotInfo_;
        info->data = out->data();
    }
    *sinceSeq = slotSeq_;
    slotConsumed_ = true;
    return true;
}

void VideoReceiver::updateRates() {
    const int64_t now = nowMs();
    const int64_t base = rateBaseMs_.load();
    if (now - base < 500) return;
    const double dt = (now - base) / 1000.0;
    rxFps_.store((framesComplete_.load() - rateBaseFrames_.load()) / dt);
    rxMbps_.store((bytesReceived_.load() - rateBaseBytes_.load()) * 8.0 / dt / 1e6);
    decodeFps_.store((framesDecoded_.load() - rateBaseDecoded_.load()) / dt);
    rateBaseFrames_.store(framesComplete_.load());
    rateBaseBytes_.store(bytesReceived_.load());
    rateBaseDecoded_.store(framesDecoded_.load());
    rateBaseMs_.store(now);
}

VideoStats VideoReceiver::stats() const {
    const_cast<VideoReceiver *>(this)->updateRates();
    VideoStats s;
    s.running = running_.load();
    s.packetsReceived = packetsReceived_.load();
    s.bytesReceived = bytesReceived_.load();
    s.framesComplete = framesComplete_.load();
    s.framesIncomplete = framesIncomplete_.load();
    s.framesAbandoned = framesAbandoned_.load();
    s.duplicateFragments = duplicateFragments_.load();
    s.out_of_order_frames = outOfOrderFrames_.load();
    s.badPackets = badPackets_.load();
    s.framesDecoded = framesDecoded_.load();
    s.decodeErrors = decodeErrors_.load();
    s.framesDroppedBySlot = droppedBySlot_.load();
    s.frameGaps = frameGaps_.load();
    s.keyframeWaits = keyframeWaits_.load();
    s.decoderResets = decoderResets_.load();
    s.rxFps = rxFps_.load();
    s.rxMbps = rxMbps_.load();
    s.decodeFps = decodeFps_.load();
    s.width = width_.load();
    s.height = height_.load();
    s.codec = static_cast<VideoCodec>(codec_.load());
    s.codecMode = static_cast<VideoCodecMode>(codecMode_.load());
    s.waitingKeyframe = waitingKeyframe_.load();
    return s;
}

} // namespace rm2027
