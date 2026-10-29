#include "rm2027/video/video_decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "rm2027/common/log.h"

namespace rm2027 {

namespace {

std::string ffErr(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, buf, sizeof(buf));
    return std::string(buf);
}

AVCodecID codecId(VideoCodec c) {
    switch (c) {
    case VideoCodec::H265: return AV_CODEC_ID_HEVC;
    case VideoCodec::H264: return AV_CODEC_ID_H264;
    case VideoCodec::Jpeg: return AV_CODEC_ID_MJPEG;
    default: return AV_CODEC_ID_NONE;
    }
}

// 找起始码之后的数据偏移（返回 nullptr 表示没找到）
const uint8_t *firstNal(const uint8_t *d, std::size_t n, std::size_t *nalStart) {
    for (std::size_t i = 0; i + 3 <= n; ++i) {
        if (d[i] == 0 && d[i + 1] == 0) {
            if (d[i + 2] == 1) {
                *nalStart = i + 3;
                return d + i + 3;
            }
            if (i + 4 <= n && d[i + 2] == 0 && d[i + 3] == 1) {
                *nalStart = i + 4;
                return d + i + 4;
            }
        }
    }
    return nullptr;
}

} // namespace

const char *videoCodecName(VideoCodec c) {
    switch (c) {
    case VideoCodec::H265: return "h265";
    case VideoCodec::H264: return "h264";
    case VideoCodec::Jpeg: return "jpeg";
    default: return "unknown";
    }
}

VideoCodec VideoDecoder::detectCodec(const uint8_t *data, std::size_t size) {
    // 只做「有把握」的判断；没把握就返回 Unknown，让调用方**保持当前解码器**。
    //
    // 踩过的两个坑：
    //  1) 对每个 AU 硬判 H.265/H.264 -> 不含参数集的 P/B 帧被误判，解码器反复切换；
    //  2) 单字节判据有歧义：HEVC 的 IDR_N_LP(type 20) 首字节 0x28，
    //     与 H.264 的 PPS(0x28) 完全相同，只看一字节必然误判。
    // 现在：扫一遍 AU 里所有 NAL，按证据计分。
    if (!data || size < 5) return VideoCodec::Unknown;

    // JPEG: FF D8 FF
    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) return VideoCodec::Jpeg;

    int score265 = 0, score264 = 0;
    std::size_t pos = 0;
    int scanned = 0;
    while (pos + 3 < size && scanned < 32) {
        // 找下一个起始码
        std::size_t i = pos;
        std::size_t nalOff = 0;
        bool found = false;
        for (; i + 3 <= size; ++i) {
            if (data[i] == 0 && data[i + 1] == 0) {
                if (data[i + 2] == 1) { nalOff = i + 3; found = true; break; }
                if (i + 4 <= size && data[i + 2] == 0 && data[i + 3] == 1) {
                    nalOff = i + 4; found = true; break;
                }
            }
        }
        if (!found || nalOff + 2 > size) break;
        pos = nalOff;
        ++scanned;

        const int b0 = data[nalOff];
        const int b1 = data[nalOff + 1];
        const int h265Type = (b0 >> 1) & 0x3F;
        const int h264Type = b0 & 0x1F;

        // HEVC 参数集：0x40/0x42/0x44（H.264 语义下是 type 0/2/4，无歧义）
        if (h265Type == 32 || h265Type == 33 || h265Type == 34) { score265 += 3; continue; }
        // H.264 参数集/AUD：0x67/0x68/0x09（HEVC 语义下不是参数集，无歧义）
        if (b0 == 0x67 || b0 == 0x68 || b0 == 0x09) { score264 += 3; continue; }
        // HEVC 的 2 字节 NAL 头：layer_id 低位为 0、temporal_id_plus1 通常为 1 -> b1 == 0x01
        if (h265Type >= 0 && h265Type <= 31) {
            if (b1 == 0x01 && (b0 & 0x81) == 0) score265 += 1;
            else if (h264Type >= 1 && h264Type <= 5) score264 += 1;
        }
    }

    if (score265 > score264) return VideoCodec::H265;
    if (score264 > score265) return VideoCodec::H264;
    return VideoCodec::Unknown;
}

bool VideoDecoder::isKeyframeAu(const uint8_t *data, std::size_t size) {
    if (!data || size < 5) return false;
    std::size_t pos = 0, off = 0;
    int scanned = 0;
    while (pos + 3 < size && scanned < 64) {
        std::size_t i = pos, nalOff = 0;
        bool found = false;
        for (; i + 3 <= size; ++i) {
            if (data[i] == 0 && data[i + 1] == 0) {
                if (data[i + 2] == 1) { nalOff = i + 3; found = true; break; }
                if (i + 4 <= size && data[i + 2] == 0 && data[i + 3] == 1) {
                    nalOff = i + 4; found = true; break;
                }
            }
        }
        if (!found || nalOff + 2 > size) break;
        pos = nalOff;
        ++scanned;
        const int b0 = data[nalOff];
        const int b1 = data[nalOff + 1];
        const int t265 = (b0 >> 1) & 0x3F;
        // H.265 关键帧：BLA(16-18) / IDR_W_RADL(19) / IDR_N_LP(20) / CRA(21)
        if (b1 == 0x01 && t265 >= 16 && t265 <= 21) return true;
        // H.264 IDR / SPS
        if (b0 == 0x65 || b0 == 0x25 || b0 == 0x45 || b0 == 0x05 || b0 == 0x67) return true;
    }
    return false;
}

struct VideoDecoder::Impl {
    AVCodecContext *ctx{nullptr};
    AVFrame *frame{nullptr};
    AVPacket *packet{nullptr};
    SwsContext *sws{nullptr};
    VideoCodec codec{VideoCodec::Unknown};
    std::vector<uint8_t> bgr;   // 解码输出的 BGR24 缓冲
    int width{0}, height{0}, stride{0};
    bool keyframe{false};
    bool flushed{false};
    int srcFormat{-1};
    int srcW{0}, srcH{0};
};

VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}
VideoDecoder::~VideoDecoder() { close(); }

bool VideoDecoder::isOpen() const { return impl_->ctx != nullptr; }
VideoCodec VideoDecoder::codec() const { return impl_->codec; }

void VideoDecoder::close() {
    if (impl_->sws) {
        sws_freeContext(impl_->sws);
        impl_->sws = nullptr;
    }
    if (impl_->frame) av_frame_free(&impl_->frame);
    if (impl_->packet) av_packet_free(&impl_->packet);
    if (impl_->ctx) avcodec_free_context(&impl_->ctx);
    impl_->codec = VideoCodec::Unknown;
    impl_->bgr.clear();
    impl_->width = impl_->height = impl_->stride = 0;
    impl_->srcFormat = -1;
    impl_->srcW = impl_->srcH = 0;
    impl_->flushed = false;
}

bool VideoDecoder::open(VideoCodec codec, std::string *error) {
    if (impl_->ctx && impl_->codec == codec) return true;
    close();

    const AVCodec *dec = avcodec_find_decoder(codecId(codec));
    if (!dec) {
        if (error) *error = std::string("找不到解码器: ") + videoCodecName(codec);
        return false;
    }
    impl_->ctx = avcodec_alloc_context3(dec);
    if (!impl_->ctx) {
        if (error) *error = "avcodec_alloc_context3 失败";
        return false;
    }
    // 低延迟 + 尽力输出任何东西（无人机/穿越机数字图传的通行做法）
    //
    // 依据：mpv 默认就打开 SHOW_ALL，理由是播放器应该"尽可能输出任何东西"，
    // 包括关键帧之前的帧和已损坏的帧；对操作手来说，带损伤的画面能看出运动趋势，
    // 比冻结画面有用得多。所以这里绝不因为"参考链断了"就停喂或丢帧。
    impl_->ctx->flags |= AV_CODEC_FLAG_LOW_DELAY | AV_CODEC_FLAG_OUTPUT_CORRUPT;
    impl_->ctx->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;
    impl_->ctx->err_recognition = AV_EF_IGNORE_ERR;
    // 关键：只开 slice 级多线程。帧级多线程（FF_THREAD_FRAME）会缓存若干帧才输出，
    // 对逐 AU 喂入的低延迟预览是致命的（实测 91 帧只吐出 4 帧）。
    impl_->ctx->thread_count = 0;
    impl_->ctx->thread_type = FF_THREAD_SLICE;

    const int ret = avcodec_open2(impl_->ctx, dec, nullptr);
    if (ret < 0) {
        if (error) *error = "avcodec_open2 失败: " + ffErr(ret);
        close();
        return false;
    }
    impl_->frame = av_frame_alloc();
    impl_->packet = av_packet_alloc();
    if (!impl_->frame || !impl_->packet) {
        if (error) *error = "分配 AVFrame/AVPacket 失败";
        close();
        return false;
    }
    impl_->codec = codec;
    RM_INFO("video-dec", "解码器已打开: " << videoCodecName(codec) << " (" << dec->name << ")");
    return true;
}

VideoDecoder::Status VideoDecoder::decode(const uint8_t *au, std::size_t size, std::string *error) {
    if (!impl_->ctx) {
        if (error) *error = "解码器未打开";
        return Status::Error;
    }
    if (!au || size == 0) {
        if (error) *error = "空 access unit";
        return Status::Error;
    }

    av_packet_unref(impl_->packet);
    impl_->packet->data = const_cast<uint8_t *>(au);
    impl_->packet->size = static_cast<int>(size);

    int ret = avcodec_send_packet(impl_->ctx, impl_->packet);
    if (ret == AVERROR(EAGAIN)) {
        // 解码器还没吃下上一个包：先收一帧腾地方，再重发，避免丢 AU
        avcodec_receive_frame(impl_->ctx, impl_->frame);
        av_frame_unref(impl_->frame);
        ret = avcodec_send_packet(impl_->ctx, impl_->packet);
    }
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        needsKeyframe_ = true;
        if (error) *error = "send_packet 失败: " + ffErr(ret);
        return Status::Error;
    }

    ret = avcodec_receive_frame(impl_->ctx, impl_->frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return Status::Buffering; // 还需要更多数据（长 GOP / B 帧）
    }
    if (ret < 0) {
        needsKeyframe_ = true;
        if (error) *error = "receive_frame 失败: " + ffErr(ret);
        return Status::Error;
    }

    const int w = impl_->frame->width;
    const int h = impl_->frame->height;
    const int fmt = impl_->frame->format;
    if (w <= 0 || h <= 0) {
        if (error) *error = "解码帧尺寸异常";
        return Status::Error;
    }

    // 分辨率/像素格式变化（或首次）时重建缩放上下文
    if (impl_->srcFormat != fmt || impl_->srcW != w || impl_->srcH != h ||
        impl_->width != w || impl_->height != h) {
        if (impl_->sws) {
            sws_freeContext(impl_->sws);
            impl_->sws = nullptr;
        }
        impl_->sws = sws_getContext(w, h, static_cast<AVPixelFormat>(fmt), w, h, AV_PIX_FMT_BGR24,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!impl_->sws) {
            if (error) *error = "sws_getContext 失败（解码输出 -> BGR24）";
            return Status::Error;
        }
        impl_->width = w;
        impl_->height = h;
        impl_->stride = w * 3;
        impl_->bgr.assign(static_cast<std::size_t>(impl_->stride) * h, 0);
        impl_->srcFormat = fmt;
        impl_->srcW = w;
        impl_->srcH = h;
        RM_INFO("video-dec", "解码输出分辨率: " << w << "x" << h << " (pix_fmt=" << fmt << ")");
    }

    uint8_t *dst[4] = {impl_->bgr.data(), nullptr, nullptr, nullptr};
    int dstStride[4] = {impl_->stride, 0, 0, 0};
    sws_scale(impl_->sws, impl_->frame->data, impl_->frame->linesize, 0, h, dst, dstStride);

    impl_->keyframe = impl_->frame->key_frame != 0;
    if (impl_->keyframe) needsKeyframe_ = false;
    av_frame_unref(impl_->frame);
    return Status::FrameReady;
}

VideoDecoder::Status VideoDecoder::flush(std::string *error) {
    if (!impl_->ctx) return Status::Buffering;
    int ret;
    if (!impl_->flushed) {
        ret = avcodec_send_packet(impl_->ctx, nullptr);
        if (ret < 0 && ret != AVERROR_EOF && ret != AVERROR(EAGAIN)) {
            if (error) *error = "flush send 失败: " + ffErr(ret);
            return Status::Error;
        }
        impl_->flushed = true;
    }
    ret = avcodec_receive_frame(impl_->ctx, impl_->frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return Status::Buffering;
    if (ret < 0) {
        if (error) *error = "flush receive 失败: " + ffErr(ret);
        return Status::Error;
    }
    const int w = impl_->frame->width, h = impl_->frame->height;
    if (impl_->srcFormat != impl_->frame->format || impl_->width != w || impl_->height != h) {
        if (impl_->sws) sws_freeContext(impl_->sws);
        impl_->sws = sws_getContext(w, h, static_cast<AVPixelFormat>(impl_->frame->format), w, h,
                                    AV_PIX_FMT_BGR24, SWS_BILINEAR, nullptr, nullptr, nullptr);
        impl_->width = w;
        impl_->height = h;
        impl_->stride = w * 3;
        impl_->bgr.assign(static_cast<std::size_t>(impl_->stride) * h, 0);
        impl_->srcFormat = impl_->frame->format;
    }
    uint8_t *dst[4] = {impl_->bgr.data(), nullptr, nullptr, nullptr};
    int dstStride[4] = {impl_->stride, 0, 0, 0};
    sws_scale(impl_->sws, impl_->frame->data, impl_->frame->linesize, 0, h, dst, dstStride);
    impl_->keyframe = impl_->frame->key_frame != 0;
    av_frame_unref(impl_->frame);
    return Status::FrameReady;
}

const uint8_t *VideoDecoder::frameData() const { return impl_->bgr.data(); }
int VideoDecoder::frameWidth() const { return impl_->width; }
int VideoDecoder::frameHeight() const { return impl_->height; }
int VideoDecoder::frameStride() const { return impl_->stride; }
bool VideoDecoder::frameIsKeyframe() const { return impl_->keyframe; }

} // namespace rm2027
