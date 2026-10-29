// RM2027 图传 —— 视频解码器（FFmpeg libavcodec + libswscale）
//
// 健壮性来自调研结论：
//   * 码流格式未必是 H.265 -> 做格式探测（H.265 / H.264 / MJPEG）
//   * GOP 可能很长 -> 中途接入要等 IDR；丢包后不重建解码器，标记等待关键帧自然重同步
//   * 分辨率不写死 -> 按解码器实际输出自适应
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rm2027/video/video_types.h"

namespace rm2027 {

class VideoDecoder {
public:
    enum class Status {
        FrameReady,     // 解出一帧，可用 frameData() 取
        Buffering,      // 数据已喂，但解码器还没吐帧（需要更多数据）
        Error,          // 解码出错（调用方应等下一个关键帧）
    };

    VideoDecoder();
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder &) = delete;
    VideoDecoder &operator=(const VideoDecoder &) = delete;

    // 探测 Annex-B 码流格式
    static VideoCodec detectCodec(const uint8_t *data, std::size_t size);

    // 这个 access unit 是不是关键帧（H.265: IDR/CRA/BLA；H.264: IDR）
    static bool isKeyframeAu(const uint8_t *data, std::size_t size);

    // 打开/切换到指定格式的解码器
    bool open(VideoCodec codec, std::string *error);
    void close();

    // 是否已经打开
    bool isOpen() const;

    // 当前解码器对应的格式
    VideoCodec codec() const;

    // 喂一个 access unit。必要时内部会自动切换解码器（按探测结果）。
    Status decode(const uint8_t *au, std::size_t size, std::string *error);

    // 冲刷解码器：取出因 B 帧重排序而缓存的帧。
    // 发送结束后调用，可把尾部几帧也解出来（返回 FrameReady 就要取，直到不再返回）。
    Status flush(std::string *error);

    // decode() 返回 FrameReady 后取图像（BGR24，紧凑排列）。
    // 指针在下一次 decode()/close() 之前有效。
    const uint8_t *frameData() const;
    int frameWidth() const;
    int frameHeight() const;
    int frameStride() const;
    bool frameIsKeyframe() const;

    // 解码器出错后置位；调用方可用它决定是否丢弃后续非关键帧
    bool needsKeyframe() const { return needsKeyframe_; }
    void clearNeedsKeyframe() { needsKeyframe_ = false; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool needsKeyframe_{true};
};

} // namespace rm2027
