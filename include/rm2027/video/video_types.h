// RM2027 图传 —— 公共类型
//
// 约定：图像统一用 **BGR24 紧凑排列**（与 OpenCV / Qt 一致），
// 这样 Python(cv2/numpy) 与 C++ 之间可以直接零拷贝传递。
#pragma once

#include <cstdint>
#include <string>

namespace rm2027 {

// 一帧图像视图。data 的所有权不在本结构体，只在回调期间有效。
struct VideoFrame {
    const uint8_t *data{nullptr}; // BGR24，逐行 stride 字节
    int width{0};
    int height{0};
    int stride{0};   // 每行字节数（通常 = width*3）
    uint64_t frameNumber{0};
    int64_t ptsUs{0};
    bool keyframe{false};
};

// 支持的编码/解码格式
enum class VideoCodec { Unknown, H265, H264, Jpeg };

// 解码格式选择模式
//   Auto      —— 默认：用探针自动探测码流格式（粘性：探不出来就沿用当前解码器）
//   H265/H264/Jpeg —— 手动固定，完全跳过探测
enum class VideoCodecMode { Auto, H265, H264, Jpeg };

const char *videoCodecName(VideoCodec c);
const char *videoCodecModeName(VideoCodecMode m);
// 解析 "auto" / "h265" / "h264" / "jpeg"（大小写不敏感，也接受 "hevc"/"h.265"）
// 无法识别时返回 false
bool videoCodecModeParse(const char *text, VideoCodecMode *out);
// 手动模式下对应的解码器；Auto 返回 Unknown
VideoCodec videoCodecModeCodec(VideoCodecMode m);

// ---------------------------------------------------------------------------
// 统计
// ---------------------------------------------------------------------------
struct VideoStats {
    // 发送侧
    uint64_t framesSent{0};
    uint64_t packetsSent{0};
    uint64_t bytesSent{0};
    uint64_t encodeErrors{0};
    // 接收组帧
    uint64_t packetsReceived{0};
    uint64_t bytesReceived{0};
    uint64_t framesComplete{0};
    uint64_t framesIncomplete{0};
    uint64_t framesAbandoned{0};   // 等待超时/被更新的帧挤掉
    uint64_t duplicateFragments{0};
    uint64_t out_of_order_frames{0};
    uint64_t badPackets{0};
    uint64_t frameGaps{0};   // 帧号出现缺口（丢包/乱序导致）
    // 解码
    uint64_t framesDecoded{0};
    uint64_t decodeErrors{0};
    uint64_t framesDroppedBySlot{0}; // 解码出来但还没被取走就被新帧覆盖
    uint64_t keyframeWaits{0};
    uint64_t decoderResets{0};
    // 速率（滑动窗口实测）
    double txFps{0}, rxFps{0}, decodeFps{0};
    double txMbps{0}, rxMbps{0};
    // 当前图像
    int width{0}, height{0};
    VideoCodec codec{VideoCodec::Unknown};
    VideoCodecMode codecMode{VideoCodecMode::Auto};
    bool waitingKeyframe{true};
    bool running{false};
};

// 统计 -> 一行 JSON（给 C API / Python / GUI 用）
std::string videoStatsToJson(const VideoStats &s);

} // namespace rm2027
