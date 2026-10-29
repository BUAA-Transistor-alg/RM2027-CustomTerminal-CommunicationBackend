// RM2027 图传 —— 接收端：UDP 组帧 -> HEVC 解码 -> 最新帧槽位 + 统计
//
// 设计要点（来自官方码流封帧调研）：
//   * 一个「帧编号」= 一个完整的 access unit，组好后直接喂解码器
//   * 按 fragmentIndex 归位，**不假设分片大小**
//   * 无重传：要统计并容忍丢包/乱序/重复/不完整帧
//   * GOP 可能很长：解码出错后不重建解码器，标记等待关键帧自然重同步
//   * 只保留最新解码帧：UI 慢时不阻塞网络与解码线程
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rm2027/video/video_decoder.h"
#include "rm2027/video/video_types.h"

namespace rm2027 {

struct VideoReceiverOptions {
    std::string bindHost{"0.0.0.0"};
    uint16_t port{3334};                 // 官方：客户端监听 3334
    std::size_t maxFrameBytes{8u << 20}; // 单帧上限（防呆）
    int frameTimeoutMs{300};             // 组帧超时：超过则判为不完整并丢弃
    std::size_t maxPendingFrames{48};    // 同时在组的帧数上限
    std::size_t auQueueDepth{64};        // 待解码 AU 队列深度（B 帧时解码器有延迟，要留够）
    // 解码格式：默认自动探测；也可手动固定为某一种
    VideoCodecMode codecMode{VideoCodecMode::Auto};
};

class VideoReceiver {
public:
    // 可选：每解出一帧同步回调（一帧不漏；预览建议用 getLatestFrame 拉取）
    using FrameHandler = std::function<void(const VideoFrame &)>;

    explicit VideoReceiver(VideoReceiverOptions opt);
    ~VideoReceiver();

    VideoReceiver(const VideoReceiver &) = delete;
    VideoReceiver &operator=(const VideoReceiver &) = delete;

    bool start(std::string *error);
    void stop();
    bool running() const { return running_.load(); }

    // 停止状态下可以换端口等
    void setOptions(const VideoReceiverOptions &opt);
    const VideoReceiverOptions &options() const { return opt_; }

    // 取最新解码帧（拷贝进 out）。*sinceSeq 传入上次取到的序号；
    // 有新帧则更新 *sinceSeq 并返回 true。没有新帧返回 false 且不写 out。
    bool getLatestFrame(uint64_t *sinceSeq, std::vector<uint8_t> *out, VideoFrame *info);

    void setFrameHandler(FrameHandler h);

    // 设置解码格式模式。**运行中也可调用**，下一个 access unit 起生效
    //（若与当前解码器不同会立刻重建解码器）。
    void setCodecMode(VideoCodecMode mode);
    VideoCodecMode codecMode() const;

    VideoStats stats() const;

private:
    struct PendingFrame {
        uint32_t expected{0};
        std::map<uint16_t, std::string> fragments;
        std::size_t got{0};
        int64_t firstSeenMs{0};
    };

    void rxLoop();
    void decodeLoop();
    void handlePacket(const uint8_t *data, std::size_t len);
    void enqueueAu(uint16_t frameNumber, std::string au);
    void storeFrame(const std::vector<uint8_t> &bgr, int w, int h, uint64_t auNo, bool key);
    void updateRates();
    void sweepStale(int64_t now);

    VideoReceiverOptions opt_;
    int socket_{-1};
    std::atomic<bool> running_{false};
    std::atomic<bool> draining_{false};
    std::thread rxThread_, decodeThread_;

    // 组帧（只在 rx 线程访问）
    std::map<uint16_t, PendingFrame> pending_;
    bool haveNext_{false};
    uint16_t nextFrame_{0};
    uint16_t lastCompletedFrame_{0};
    bool haveLastCompleted_{false};

    // AU 队列
    std::mutex auMutex_;
    std::condition_variable auCv_;
    // (发送端帧编号, AU 数据)
    std::deque<std::pair<uint16_t, std::string>> auQueue_;
    uint64_t auSeq_{0};

    // 最新帧槽位
    std::mutex slotMutex_;
    std::vector<uint8_t> slotData_;
    uint64_t slotSeq_{0};
    bool slotConsumed_{true};  // 上一帧是否已被取走
    VideoFrame slotInfo_;

    std::unique_ptr<VideoDecoder> decoder_;
    std::mutex decoderMutex_;
    FrameHandler handler_;

    // 统计
    std::atomic<uint64_t> packetsReceived_{0}, bytesReceived_{0};
    std::atomic<uint64_t> framesComplete_{0}, framesIncomplete_{0}, framesAbandoned_{0};
    std::atomic<uint64_t> duplicateFragments_{0}, outOfOrderFrames_{0}, badPackets_{0};
    std::atomic<uint64_t> framesDecoded_{0}, decodeErrors_{0}, droppedBySlot_{0};
    std::atomic<uint64_t> keyframeWaits_{0}, decoderResets_{0};
    std::atomic<uint64_t> frameGaps_{0};
    // 帧号连续性 + 关键帧门控：参考链一断，绝不能拿过期参考硬解（会出错误画面）
    bool lastAuSeen_{false};
    uint16_t lastAuFrame_{0};
    bool needKeyframe_{true};
    std::atomic<double> rxFps_{0}, rxMbps_{0}, decodeFps_{0};
    std::atomic<int> width_{0}, height_{0};
    std::atomic<int> codec_{0};
    std::atomic<int> codecMode_{0};   // VideoCodecMode
    std::atomic<bool> waitingKeyframe_{true};

    std::atomic<uint64_t> rateBaseFrames_{0}, rateBaseBytes_{0}, rateBaseDecoded_{0};
    std::atomic<int64_t> rateBaseMs_{0};
};

} // namespace rm2027
