// RM2027 图传 —— 发送端：吃图片、编 HEVC、打包 UDP
//
// 与旧版（直接发 .h265 裸流）的区别：编码在 C++ 内部完成，
// 调用方只负责把一帧帧 BGR24 图像喂进来。
//
// 另带一组「故障注入」，专门用来压测接收端的稳定性。
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rm2027/video/hevc_encoder.h"
#include "rm2027/video/video_types.h"

namespace rm2027 {

struct VideoSenderOptions {
    std::string targetHost{"127.0.0.1"};
    uint16_t targetPort{3334};
    std::size_t maxDatagram{1400};      // 含 8 字节包头
    EncoderConfig encoder;              // 默认 1920x1080@60

    // ---- 网络故障注入（0/false = 关闭）----
    int dropEveryNthPacket{0};          // 每 N 个包丢 1 个
    int duplicateEveryNthPacket{0};     // 每 N 个包重复发一次
    bool shuffleFragments{false};       // 打乱同一帧内分片的发送顺序
    bool corruptFrameTotal{false};      // 故意写错「当前帧总字节数」
    int frameNumberStep{1};             // 帧号步进（>1 制造跳号）

    // ---- 编码配置轮换（压测：中途换参数/换分辨率）----
    int rotateEncoderEveryNFrames{0};   // >0 时每 N 帧换一套编码配置
};

class VideoSender {
public:
    explicit VideoSender(VideoSenderOptions opt);
    ~VideoSender();

    VideoSender(const VideoSender &) = delete;
    VideoSender &operator=(const VideoSender &) = delete;

    bool start(std::string *error);
    void stop();
    bool running() const { return running_.load(); }

    // 喂一帧 BGR24（stride 字节/行）。编码 + 打包 + 发送在本调用内同步完成。
    bool sendFrame(const uint8_t *bgr, int width, int height, int stride, std::string *error);

    // 发送结束前调用，取出编码器里缓存的 AU 并发出（含 B 帧时必须有）
    bool flush(std::string *error);

    // 停止状态下可以换配置（端口、编码参数等）
    void setOptions(const VideoSenderOptions &opt);
    const VideoSenderOptions &options() const { return opt_; }

    // 立即切换编码配置（运行中也允许 -> 会产生中途参数集变化，本身就是压测项）
    bool setEncoderConfig(const EncoderConfig &cfg, std::string *error);

    VideoStats stats() const;
    const EncoderConfig &encoderConfig() const;

    // 预置的压力配置集合（第一条是"正常"配置）
    static std::vector<EncoderConfig> stressProfiles(const EncoderConfig &base);

private:
    bool sendAccessUnit(const std::string &au, std::string *error);
    void updateRates();

    VideoSenderOptions opt_;
    std::unique_ptr<HevcEncoder> encoder_;
    int socket_{-1};
    std::atomic<bool> running_{false};

    std::atomic<uint64_t> framesSent_{0}, packetsSent_{0}, bytesSent_{0}, encodeErrors_{0};
    std::atomic<uint64_t> auCount_{0};
    uint16_t frameNumber_{0};

    // 速率统计
    std::atomic<uint64_t> rateBaseFrames_{0}, rateBaseBytes_{0};
    std::atomic<int64_t> rateBaseMs_{0};
    std::atomic<double> txFps_{0}, txMbps_{0};
    int64_t lastPacketDecision_{0};
    std::atomic<int> profileIndex_{0};
    std::vector<EncoderConfig> profiles_;
};

} // namespace rm2027
