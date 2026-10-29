#include "rm2027/video/hevc_encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <sstream>

#include "rm2027/common/log.h"

namespace rm2027 {

namespace {

AVPixelFormat parsePixFmt(const std::string &name) {
    if (name == "yuv420p") return AV_PIX_FMT_YUV420P;
    if (name == "yuv420p10le") return AV_PIX_FMT_YUV420P10LE;
    if (name == "yuv422p") return AV_PIX_FMT_YUV422P;
    if (name == "yuv422p10le") return AV_PIX_FMT_YUV422P10LE;
    if (name == "yuv444p") return AV_PIX_FMT_YUV444P;
    if (name == "yuv444p10le") return AV_PIX_FMT_YUV444P10LE;
    return AV_PIX_FMT_NONE;
}

std::string ffErr(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, buf, sizeof(buf));
    return std::string(buf);
}

} // namespace

std::string EncoderConfig::describe() const {
    std::ostringstream os;
    os << width << "x" << height << "@" << fps << " " << pixFmt << " " << bitrateKbps << "kbps"
       << " gop=" << gopSize << " b=" << bFrames << " slices=" << slices
       << " aud=" << (aud ? 1 : 0) << " repeatHdr=" << (repeatHeaders ? 1 : 0)
       << " preset=" << preset;
    if (!tune.empty()) os << " tune=" << tune;
    return os.str();
}

struct HevcEncoder::Impl {
    AVCodecContext *ctx{nullptr};
    AVFrame *frame{nullptr};
    AVPacket *packet{nullptr};
    SwsContext *sws{nullptr};
    EncoderConfig cfg;
    int srcWidth{0}, srcHeight{0};
    int64_t nextPts{0};
    bool flushed{false};
    std::vector<uint8_t> scratch; // 缩放目标缓冲（AVFrame 自己管内存，这里备用）
};

HevcEncoder::HevcEncoder() : impl_(std::make_unique<Impl>()) {}

HevcEncoder::~HevcEncoder() { close(); }

bool HevcEncoder::isOpen() const { return impl_->ctx != nullptr; }

const EncoderConfig &HevcEncoder::config() const { return impl_->cfg; }

bool HevcEncoder::hasReordering() const { return impl_->cfg.bFrames > 0; }

bool HevcEncoder::flushed() const { return impl_->ctx != nullptr && impl_->flushed; }

bool HevcEncoder::open(const EncoderConfig &cfg, std::string *error) {
    close();
    impl_->cfg = cfg;
    impl_->flushed = false;
    impl_->nextPts = 0;

    const AVCodec *codec = avcodec_find_encoder_by_name("libx265");
    if (codec == nullptr) codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
    if (codec == nullptr) {
        lastError_ = "找不到 HEVC 编码器（缺 libx265？）";
        if (error) *error = lastError_;
        return false;
    }

    impl_->ctx = avcodec_alloc_context3(codec);
    if (!impl_->ctx) {
        lastError_ = "avcodec_alloc_context3 失败";
        if (error) *error = lastError_;
        return false;
    }

    AVCodecContext *c = impl_->ctx;
    c->width = cfg.width;
    c->height = cfg.height;
    c->time_base = AVRational{1, cfg.fps};
    c->framerate = AVRational{cfg.fps, 1};
    c->gop_size = cfg.gopSize > 0 ? cfg.gopSize : 1;
    c->max_b_frames = cfg.bFrames;
    c->bit_rate = static_cast<int64_t>(cfg.bitrateKbps) * 1000;

    const AVPixelFormat pix = parsePixFmt(cfg.pixFmt);
    if (pix == AV_PIX_FMT_NONE) {
        lastError_ = "不支持的像素格式: " + cfg.pixFmt;
        if (error) *error = lastError_;
        close();
        return false;
    }
    c->pix_fmt = pix;
    // 10bit 要给足码率，否则画质崩
    if (cfg.pixFmt.find("10le") != std::string::npos) c->bit_rate *= 2;

    // ---- x265 参数 ----
    // x265 硬性要求：rc-lookahead > 最大连续 B 帧数，否则直接拒绝打开编码器。
    // 这里做兜底，免得每个调用方都要自己记得配。
    int lookahead = cfg.x265Lookahead;
    if (cfg.bFrames > 0 && lookahead <= cfg.bFrames) {
        lookahead = cfg.bFrames + 1;
        RM_WARN("hevc-enc", "bframes=" << cfg.bFrames << " 要求 rc-lookahead > bframes，"
                                       << "自动把 lookahead 调整为 " << lookahead);
    }
    // zerolatency 与 B 帧语义冲突，同时给会被 x265 拒绝
    std::string tune = cfg.tune;
    if (cfg.bFrames > 0 && tune == "zerolatency") {
        RM_WARN("hevc-enc", "bframes>0 与 tune=zerolatency 冲突，已忽略 tune");
        tune.clear();
    }

    std::ostringstream x265;
    x265 << "log-level=error";
    if (cfg.gopSize > 0) x265 << ":keyint=" << cfg.gopSize << ":min-keyint=" << cfg.gopSize;
    else x265 << ":keyint=-1"; // 只发第一个 IDR
    x265 << ":bframes=" << cfg.bFrames;
    if (cfg.slices > 1) x265 << ":slices=" << cfg.slices;
    x265 << ":aud=" << (cfg.aud ? 1 : 0);
    x265 << ":repeat-headers=" << (cfg.repeatHeaders ? 1 : 0);
    x265 << ":rc-lookahead=" << lookahead;
    if (cfg.intraRefresh) x265 << ":intra-refresh=1";
    x265 << ":scenecut=" << cfg.scenecut;
    if (!cfg.extraX265Params.empty()) x265 << ":" << cfg.extraX265Params;

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "preset", cfg.preset.c_str(), 0);
    if (!tune.empty()) av_dict_set(&opts, "tune", tune.c_str(), 0);
    av_dict_set(&opts, "x265-params", x265.str().c_str(), 0);
    // 不要全局头：参数集要跟着码流 in-band 走
    c->flags &= ~AV_CODEC_FLAG_GLOBAL_HEADER;

    const int ret = avcodec_open2(c, codec, &opts);
    if (ret < 0) {
        lastError_ = "avcodec_open2 失败: " + ffErr(ret);
        av_dict_free(&opts);
        close();
        if (error) *error = lastError_;
        return false;
    }
    if (opts) {
        AVDictionaryEntry *e = nullptr;
        while ((e = av_dict_get(opts, "", e, AV_DICT_IGNORE_SUFFIX)) != nullptr) {
            RM_WARN("hevc-enc", "编码器未识别的选项: " << e->key << "=" << e->value);
        }
    }
    av_dict_free(&opts);

    impl_->frame = av_frame_alloc();
    impl_->packet = av_packet_alloc();
    if (!impl_->frame || !impl_->packet) {
        lastError_ = "av_frame_alloc/av_packet_alloc 失败";
        close();
        if (error) *error = lastError_;
        return false;
    }
    impl_->frame->format = c->pix_fmt;
    impl_->frame->width = c->width;
    impl_->frame->height = c->height;
    const int fr = av_frame_get_buffer(impl_->frame, 32);
    if (fr < 0) {
        lastError_ = "av_frame_get_buffer 失败: " + ffErr(fr);
        close();
        if (error) *error = lastError_;
        return false;
    }

    RM_INFO("hevc-enc", "HEVC 编码器已打开: " << cfg.describe());
    return true;
}

void HevcEncoder::close() {
    if (impl_->sws) {
        sws_freeContext(impl_->sws);
        impl_->sws = nullptr;
    }
    if (impl_->frame) av_frame_free(&impl_->frame);
    if (impl_->packet) av_packet_free(&impl_->packet);
    if (impl_->ctx) avcodec_free_context(&impl_->ctx);
    impl_->srcWidth = impl_->srcHeight = 0;
    impl_->nextPts = 0;
    impl_->flushed = false;
}

namespace {
// 把编码器当前可取的包全部取出
bool drainPackets(AVCodecContext *c, AVPacket *pkt, std::vector<std::string> *out,
                  std::string *error) {
    while (true) {
        const int ret = avcodec_receive_packet(c, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return true;
        if (ret < 0) {
            if (error) *error = "avcodec_receive_packet 失败: " + ffErr(ret);
            return false;
        }
        out->emplace_back(reinterpret_cast<const char *>(pkt->data), pkt->size);
        av_packet_unref(pkt);
    }
}
} // namespace

bool HevcEncoder::encodeBgr(const uint8_t *bgr, int width, int height, int stride, int64_t ptsUs,
                            std::vector<std::string> *accessUnits, std::string *error) {
    if (!impl_->ctx) {
        lastError_ = "编码器未打开";
        if (error) *error = lastError_;
        return false;
    }
    if (!bgr || width <= 0 || height <= 0) {
        lastError_ = "无效的图像参数";
        if (error) *error = lastError_;
        return false;
    }

    // 输入分辨率变化：重建缩放上下文（必要时也重建编码器尺寸）
    if (width != impl_->srcWidth || height != impl_->srcHeight) {
        if (impl_->sws) {
            sws_freeContext(impl_->sws);
            impl_->sws = nullptr;
        }
        impl_->sws = sws_getContext(width, height, AV_PIX_FMT_BGR24, impl_->cfg.width,
                                    impl_->cfg.height, impl_->ctx->pix_fmt, SWS_BILINEAR, nullptr,
                                    nullptr, nullptr);
        if (!impl_->sws) {
            lastError_ = "sws_getContext 失败（输入 " + std::to_string(width) + "x" +
                         std::to_string(height) + "）";
            if (error) *error = lastError_;
            return false;
        }
        impl_->srcWidth = width;
        impl_->srcHeight = height;
    }

    const int mr = av_frame_make_writable(impl_->frame);
    if (mr < 0) {
        lastError_ = "av_frame_make_writable 失败: " + ffErr(mr);
        if (error) *error = lastError_;
        return false;
    }

    const uint8_t *srcSlice[4] = {bgr, nullptr, nullptr, nullptr};
    const int srcStride[4] = {stride, 0, 0, 0};
    sws_scale(impl_->sws, srcSlice, srcStride, 0, height, impl_->frame->data, impl_->frame->linesize);

    impl_->frame->pts = impl_->nextPts++;

    const int sr = avcodec_send_frame(impl_->ctx, impl_->frame);
    if (sr < 0) {
        lastError_ = "avcodec_send_frame 失败: " + ffErr(sr);
        if (error) *error = lastError_;
        return false;
    }
    return drainPackets(impl_->ctx, impl_->packet, accessUnits, error);
}

bool HevcEncoder::flush(std::vector<std::string> *accessUnits, std::string *error) {
    if (!impl_->ctx) return true;
    impl_->flushed = true;
    const int sr = avcodec_send_frame(impl_->ctx, nullptr);
    if (sr < 0 && sr != AVERROR_EOF) {
        lastError_ = "flush send_frame 失败: " + ffErr(sr);
        if (error) *error = lastError_;
        return false;
    }
    return drainPackets(impl_->ctx, impl_->packet, accessUnits, error);
}

} // namespace rm2027
