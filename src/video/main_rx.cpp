// RM2027 图传接收工具 —— VideoReceiver：UDP 组帧 -> HEVC 解码 -> 统计/落盘
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <fstream>

#include "rm2027/common/log.h"
#include "rm2027/video/video_receiver.h"

namespace {
rm2027::VideoReceiver *g_rx = nullptr;
void onSignal(int) { if (g_rx) g_rx->stop(); }

void writePpm(const std::string &path, const std::vector<uint8_t> &bgr, int w, int h) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return;
    f << "P6\n" << w << " " << h << "\n255\n";
    // BGR -> RGB
    std::vector<uint8_t> rgb(bgr.size());
    for (std::size_t i = 0; i + 2 < bgr.size(); i += 3) {
        rgb[i] = bgr[i + 2];
        rgb[i + 1] = bgr[i + 1];
        rgb[i + 2] = bgr[i];
    }
    f.write(reinterpret_cast<const char *>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
}

void usage(const char *a) {
    std::printf(
        "RM2027 图传接收工具（UDP -> 组帧 -> HEVC 解码）\n\n用法: %s [选项]\n\n"
        "  --bind <ip>          绑定地址，默认 0.0.0.0\n"
        "  --codec <mode>       解码格式：auto（默认，探针自动选择）| h265 | h264 | jpeg\n"
        "  --port <n>           监听端口，默认 3334（官方）\n"
        "  --duration <sec>     接收时长，0 = 直到 Ctrl-C，默认 0\n"
        "  --stats-interval <ms> 统计 JSON 打印间隔，默认 1000；0 = 不打印\n"
        "  --dump <prefix>      把解码帧写成 PPM（prefix_0001.ppm），用于校验画面\n"
        "  --dump-every <n>     每 N 帧落一张，默认 30\n"
        "  --dump-max <n>       最多落盘几张，默认 8\n"
        "  --log <level>        debug|info|warn|error\n",
        a);
}
} // namespace

int main(int argc, char **argv) {
    rm2027::VideoReceiverOptions opt;
    int durationSec = 0, statsIntervalMs = 1000, dumpEvery = 30, dumpMax = 8;
    std::string dumpPrefix;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char *w) -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "选项 %s 缺少参数\n", w); std::exit(2); }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else if (a == "--bind") opt.bindHost = next("--bind");
        else if (a == "--codec") {
            const std::string m = next("--codec");
            if (!rm2027::videoCodecModeParse(m.c_str(), &opt.codecMode)) {
                std::fprintf(stderr, "无法识别的解码格式模式: %s（可选 auto/h265/h264/jpeg）\n", m.c_str());
                return 2;
            }
        }
        else if (a == "--port") opt.port = static_cast<uint16_t>(std::stoi(next("--port")));
        else if (a == "--duration") durationSec = std::stoi(next("--duration"));
        else if (a == "--stats-interval") statsIntervalMs = std::stoi(next("--stats-interval"));
        else if (a == "--dump") dumpPrefix = next("--dump");
        else if (a == "--dump-every") dumpEvery = std::stoi(next("--dump-every"));
        else if (a == "--dump-max") dumpMax = std::stoi(next("--dump-max"));
        else if (a == "--log") {
            const std::string lv = next("--log");
            if (lv == "debug") rm2027::setLogLevel(rm2027::LogLevel::Debug);
            else if (lv == "warn") rm2027::setLogLevel(rm2027::LogLevel::Warn);
            else if (lv == "error") rm2027::setLogLevel(rm2027::LogLevel::Error);
        } else { std::fprintf(stderr, "未知选项: %s\n", a.c_str()); usage(argv[0]); return 2; }
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    rm2027::VideoReceiver rx(opt);
    g_rx = &rx;
    std::string err;
    if (!rx.start(&err)) { RM_ERROR("main", err); return 1; }
    std::printf("解码格式模式: %s\n", rm2027::videoCodecModeName(rx.codecMode()));

    std::vector<uint8_t> frame;
    rm2027::VideoFrame info{};
    uint64_t seq = 0;
    int dumped = 0;
    const auto t0 = std::chrono::steady_clock::now();
    auto lastStats = t0;

    while (rx.running()) {
        if (rx.getLatestFrame(&seq, &frame, &info)) {
            if (!dumpPrefix.empty() && seq % static_cast<uint64_t>(dumpEvery == 0 ? 1 : dumpEvery) == 0 &&
                dumped < dumpMax && info.width > 0) {
                char path[512];
                ++dumped;
                // 用发送端的帧编号命名，便于按图案反推校验
                std::snprintf(path, sizeof(path), "%s_f%05llu.ppm", dumpPrefix.c_str(),
                              static_cast<unsigned long long>(info.frameNumber));
                writePpm(path, frame, info.width, info.height);
                RM_INFO("main", "已落盘解码帧 -> " << path << " (" << info.width << "x"
                                                   << info.height << ")");
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (statsIntervalMs > 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStats).count() >=
                statsIntervalMs) {
            lastStats = now;
            std::printf("%s\n", rm2027::videoStatsToJson(rx.stats()).c_str());
            std::fflush(stdout);
        }
        if (durationSec > 0 && now - t0 >= std::chrono::seconds(durationSec)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    rx.stop();
    std::printf("%s\n", rm2027::videoStatsToJson(rx.stats()).c_str());
    return 0;
}
