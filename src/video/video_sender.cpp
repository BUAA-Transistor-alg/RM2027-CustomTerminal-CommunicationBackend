#include "rm2027/video/video_sender.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>

#include "rm2027/common/log.h"
#include "rm2027/video/udp_header.h"

namespace rm2027 {

namespace {
int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

std::vector<EncoderConfig> VideoSender::stressProfiles(const EncoderConfig &base) {
    // 第一条是"正常"配置；其余逐项加压，专门用来搞客户端。
    std::vector<EncoderConfig> v;
    v.push_back(base); // 0: 正常

    {   // 1: B 帧（解码顺序 ≠ 显示顺序）
        EncoderConfig c = base;
        c.bFrames = 3;
        c.tune.clear();
        c.x265Lookahead = 10;
        c.describe();
        v.push_back(c);
    }
    {   // 2: 多 slice（一个 AU 里多个 VCL NAL）
        EncoderConfig c = base;
        c.slices = 4;
        v.push_back(c);
    }
    {   // 3: 长 GOP + 参数集只发一次（丢包后长时间无法恢复）
        EncoderConfig c = base;
        c.gopSize = 300;
        c.repeatHeaders = false;
        v.push_back(c);
    }
    {   // 4: 全 IDR（每帧都是关键帧）
        EncoderConfig c = base;
        c.gopSize = 1;
        v.push_back(c);
    }
    {   // 5: 带 AUD
        EncoderConfig c = base;
        c.aud = true;
        v.push_back(c);
    }
    {   // 6: 10bit 4:2:0
        EncoderConfig c = base;
        c.pixFmt = "yuv420p10le";
        v.push_back(c);
    }
    {   // 7: 4:4:4 8bit
        EncoderConfig c = base;
        c.pixFmt = "yuv444p";
        v.push_back(c);
    }
    {   // 8: 慢 preset（画质高、码率分布不同）
        EncoderConfig c = base;
        c.preset = "medium";
        c.tune.clear();
        c.gopSize = 120;
        v.push_back(c);
    }
    {   // 9: 30fps（时间戳步长变化）
        EncoderConfig c = base;
        c.fps = 30;
        v.push_back(c);
    }
    {   // 10: intra-refresh（不用大 IDR，靠逐列 I 宏块自愈）
        EncoderConfig c = base;
        c.intraRefresh = true;
        v.push_back(c);
    }
    {   // 11: 中途换分辨率（会重发 SPS，测试客户端分辨率自适应）
        EncoderConfig c = base;
        c.width = 1280;
        c.height = 720;
        v.push_back(c);
    }
    return v;
}

VideoSender::VideoSender(VideoSenderOptions opt) : opt_(std::move(opt)) {
    encoder_ = std::make_unique<HevcEncoder>();
}

VideoSender::~VideoSender() { stop(); }

void VideoSender::setOptions(const VideoSenderOptions &opt) {
    if (running_.load()) {
        RM_WARN("video-tx", "运行中改配置被忽略（请先 stop）");
        return;
    }
    opt_ = opt;
}

bool VideoSender::start(std::string *error) {
    if (running_.load()) return true;

    std::string err;
    if (!encoder_->open(opt_.encoder, &err)) {
        if (error) *error = err;
        return false;
    }
    profiles_ = stressProfiles(opt_.encoder);
    profileIndex_.store(0);

    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
        if (error) *error = std::string("socket() 失败: ") + std::strerror(errno);
        return false;
    }

    // 发送缓冲开大一点，避免 1080p60 突发时丢在本地
    int sndbuf = 8 * 1024 * 1024;
    ::setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    running_.store(true);
    rateBaseFrames_.store(0);
    rateBaseBytes_.store(0);
    rateBaseMs_.store(nowMs());
    RM_INFO("video-tx", "图传发送端已启动: " << opt_.targetHost << ":" << opt_.targetPort << " | "
                                             << opt_.encoder.describe()
                                             << " | 单包上限 " << opt_.maxDatagram);
    return true;
}

void VideoSender::stop() {
    if (!running_.exchange(false)) return;
    if (socket_ >= 0) {
        ::close(socket_);
        socket_ = -1;
    }
    if (encoder_) encoder_->close();
    RM_INFO("video-tx", "图传发送端已停止: 帧 " << framesSent_.load() << " / 包 "
                                                << packetsSent_.load() << " / "
                                                << bytesSent_.load() << " 字节");
}

bool VideoSender::setEncoderConfig(const EncoderConfig &cfg, std::string *error) {
    if (!running_.load()) {
        opt_.encoder = cfg;
        return true;
    }
    std::string err;
    if (!encoder_->open(cfg, &err)) { // 会 close 再 open，产生新的参数集
        if (error) *error = err;
        return false;
    }
    opt_.encoder = cfg;
    RM_INFO("video-tx", "编码配置已切换: " << cfg.describe());
    return true;
}

bool VideoSender::sendAccessUnit(const std::string &au, std::string *error) {
    if (au.empty()) return true;

    const std::size_t payloadPerPacket =
        (opt_.maxDatagram > kUdpVideoHeaderSize) ? (opt_.maxDatagram - kUdpVideoHeaderSize) : 1392;
    const std::size_t total = au.size();
    std::size_t packetCount = (total + payloadPerPacket - 1) / payloadPerPacket;

    ++frameNumber_; // 从 1 开始，自然回绕

    if (opt_.frameNumberStep > 1) frameNumber_ += static_cast<uint16_t>(opt_.frameNumberStep - 1);

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(opt_.targetPort);
    if (::inet_pton(AF_INET, opt_.targetHost.c_str(), &dst.sin_addr) != 1) {
        if (error) *error = "无法解析目标地址: " + opt_.targetHost;
        return false;
    }

    std::vector<std::uint16_t> order(packetCount);
    for (std::size_t i = 0; i < packetCount; ++i) order[i] = static_cast<uint16_t>(i);
    if (opt_.shuffleFragments && packetCount > 1) {
        std::mt19937 rng(static_cast<unsigned>(frameNumber_));
        std::shuffle(order.begin(), order.end(), rng);
    }

    const uint32_t declaredTotal =
        opt_.corruptFrameTotal ? static_cast<uint32_t>(total + 7) : static_cast<uint32_t>(total);

    std::string packet;
    for (std::size_t k = 0; k < packetCount; ++k) {
        const std::size_t i = order[k];
        const std::size_t off = i * payloadPerPacket;
        const std::size_t len = std::min(payloadPerPacket, total - off);

        UdpVideoHeader h;
        h.frameNumber = frameNumber_;
        h.fragmentIndex = static_cast<uint16_t>(i);
        h.frameTotalBytes = declaredTotal;

        packet.resize(kUdpVideoHeaderSize + len);
        writeUdpVideoHeader(reinterpret_cast<uint8_t *>(&packet[0]), h);
        std::memcpy(&packet[kUdpVideoHeaderSize], au.data() + off, len);

        ++lastPacketDecision_;
        const bool drop = opt_.dropEveryNthPacket > 0 &&
                          (lastPacketDecision_ % opt_.dropEveryNthPacket) == 0;
        const bool dup = opt_.duplicateEveryNthPacket > 0 &&
                         (lastPacketDecision_ % opt_.duplicateEveryNthPacket) == 0;

        if (!drop) {
            if (::sendto(socket_, packet.data(), packet.size(), 0,
                         reinterpret_cast<sockaddr *>(&dst), sizeof(dst)) >= 0) {
                packetsSent_.fetch_add(1);
                bytesSent_.fetch_add(packet.size());
            }
            if (dup) {
                if (::sendto(socket_, packet.data(), packet.size(), 0,
                             reinterpret_cast<sockaddr *>(&dst), sizeof(dst)) >= 0) {
                    packetsSent_.fetch_add(1);
                    bytesSent_.fetch_add(packet.size());
                }
            }
        }
    }
    framesSent_.fetch_add(1);
    return true;
}

bool VideoSender::sendFrame(const uint8_t *bgr, int width, int height, int stride,
                            std::string *error) {
    if (!running_.load()) {
        if (error) *error = "发送端未启动";
        return false;
    }

    // 配置轮换（压测）
    if (opt_.rotateEncoderEveryNFrames > 0 && !profiles_.empty()) {
        const uint64_t n = auCount_.fetch_add(1);
        if (n > 0 && (n % static_cast<uint64_t>(opt_.rotateEncoderEveryNFrames)) == 0) {
            const int idx = (profileIndex_.fetch_add(1) + 1) % static_cast<int>(profiles_.size());
            std::string err;
            if (!encoder_->open(profiles_[idx], &err)) {
                RM_WARN("video-tx", "轮换编码配置失败: " << err);
            } else {
                RM_INFO("video-tx", "编码配置轮换 -> [" << idx << "] " << profiles_[idx].describe());
            }
        }
    }

    // flush() 之后编码器处于 EOF 状态，再发帧会失败。
    // 这里自动重开（会发出新的参数集 + 关键帧，对码流是合法的），
    // 免得调用方踩坑。
    if (encoder_->flushed()) {
        std::string reopenErr;
        if (!encoder_->open(opt_.encoder, &reopenErr)) {
            encodeErrors_.fetch_add(1);
            if (error) *error = "flush 后重开编码器失败: " + reopenErr;
            return false;
        }
        RM_INFO("video-tx", "检测到编码器已 flush，自动重开以继续发送");
    }

    std::vector<std::string> aus;
    std::string err;
    if (!encoder_->encodeBgr(bgr, width, height, stride, 0, &aus, &err)) {
        encodeErrors_.fetch_add(1);
        if (error) *error = err;
        return false;
    }
    for (const auto &au : aus) {
        if (!sendAccessUnit(au, error)) return false;
    }

    // 速率
    const int64_t now = nowMs();
    const int64_t base = rateBaseMs_.load();
    if (now - base >= 500) {
        const uint64_t f = framesSent_.load();
        const uint64_t b = bytesSent_.load();
        const double dt = (now - base) / 1000.0;
        txFps_.store((f - rateBaseFrames_.load()) / dt);
        txMbps_.store((b - rateBaseBytes_.load()) * 8.0 / dt / 1e6);
        rateBaseFrames_.store(f);
        rateBaseBytes_.store(b);
        rateBaseMs_.store(now);
    }
    return true;
}

bool VideoSender::flush(std::string *error) {
    if (!running_.load() || !encoder_) return true;
    std::vector<std::string> aus;
    std::string err;
    if (!encoder_->flush(&aus, &err)) {
        if (error) *error = err;
        return false;
    }
    for (const auto &au : aus) {
        if (!sendAccessUnit(au, error)) return false;
    }
    return true;
}

VideoStats VideoSender::stats() const {
    VideoStats s;
    s.running = running_.load();
    s.framesSent = framesSent_.load();
    s.packetsSent = packetsSent_.load();
    s.bytesSent = bytesSent_.load();
    s.encodeErrors = encodeErrors_.load();
    s.txFps = txFps_.load();
    s.txMbps = txMbps_.load();
    s.width = opt_.encoder.width;
    s.height = opt_.encoder.height;
    s.codec = VideoCodec::H265;
    return s;
}

const EncoderConfig &VideoSender::encoderConfig() const { return opt_.encoder; }

} // namespace rm2027
