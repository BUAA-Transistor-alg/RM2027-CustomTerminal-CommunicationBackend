// RM2027 图传 —— HEVC 编码器（FFmpeg libavcodec + libswscale）
//
// 设计目标：
//   * 输入是图像（BGR24），输出是一个 **完整的 access unit**（Annex-B 字节）
//   * 编码参数尽可能可调 —— 用来构造各种「难搞」的码流压测客户端
//
// 官方要求 1920x1080@60fps；输入分辨率不符时用 swscale 缩放。
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rm2027/video/video_types.h"

namespace rm2027 {

// 编码参数。默认值 = 贴近官方规格的「正常」配置。
struct EncoderConfig {
    // ---- 输出规格（默认严格对齐官方 1920x1080@60）----
    int width{1920};
    int height{1080};
    int fps{60};
    int bitrateKbps{8000};

    // ---- 压力测试维度（默认都是"正常"值）----
    int gopSize{60};          // 关键帧间隔；1 = 全 IDR；0 = 只发第一个 IDR
    int bFrames{0};           // B 帧数量（>0 时输出顺序 ≠ 输入顺序）
    int slices{1};            // 每帧 slice 数（>1 时一个 AU 含多个 VCL NAL）
    bool aud{false};          // 是否插入 AUD(35)
    bool repeatHeaders{true}; // 参数集是否随每个 IDR 重复
    bool intraRefresh{false}; // 用周期性 intra-refresh 代替 IDR（低延迟链路通行做法，
                              // 丢包后画面在若干帧内自愈，不需要等下一个大关键帧）
    std::string pixFmt{"yuv420p"};  // yuv420p / yuv420p10le / yuv422p / yuv444p
    std::string preset{"ultrafast"};
    std::string tune{"zerolatency"}; // 置空则不加 tune
    int x265Lookahead{0};
    int scenecut{0};
    std::string extraX265Params;     // 追加任意 x265 参数（原样拼接）

    // ---- 缩放 ----
    bool allowScale{true};    // 输入分辨率不同则缩放；false 则报错

    std::string describe() const;
};

// 编码器：线程不安全，由调用方串行使用
class HevcEncoder {
public:
    HevcEncoder();
    ~HevcEncoder();

    HevcEncoder(const HevcEncoder &) = delete;
    HevcEncoder &operator=(const HevcEncoder &) = delete;

    // 打开编码器。成功后可连续调用 encodeBgr()。
    bool open(const EncoderConfig &cfg, std::string *error);

    // 关闭并复位（允许换配置重新 open）
    void close();

    bool isOpen() const;
    const EncoderConfig &config() const;

    // 输入一帧 BGR24 图像，输出 0..N 个完整 access unit（Annex-B 字节）。
    // 含 B 帧时，一次输入可能暂时不产生输出（由 lookahead 决定），
    // 也可能吐出多个 AU，调用方需按顺序全部发送。
    bool encodeBgr(const uint8_t *bgr, int width, int height, int stride, int64_t ptsUs,
                   std::vector<std::string> *accessUnits, std::string *error);

    // 冲刷编码器（发送结束前调用），取出缓存的 AU
    bool flush(std::vector<std::string> *accessUnits, std::string *error);

    // 最近一次出错的文字
    const std::string &lastError() const { return lastError_; }

    // 该配置是否可能产生「解码顺序 ≠ 显示顺序」
    bool hasReordering() const;

    // 是否已经 flush 过（flush 之后再编码需要重新 open）
    bool flushed() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string lastError_;
};

} // namespace rm2027
