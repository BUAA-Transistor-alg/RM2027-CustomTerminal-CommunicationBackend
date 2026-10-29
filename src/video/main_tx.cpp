// RM2027 图传发送工具 —— 生成合成图案并喂给 VideoSender（C++ 内部编码）
//
// 用途：不依赖 Python 就能压测接收端；配合 rm_video_rx 做端到端验证。
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "rm2027/common/log.h"
#include "rm2027/video/video_sender.h"

namespace {

rm2027::VideoSender *g_tx = nullptr;
void onSignal(int) { if (g_tx) g_tx->stop(); }

// 生成一帧可校验的合成图案（BGR24）
void makePattern(std::vector<uint8_t> &buf, int w, int h, int frame) {
    buf.resize(static_cast<std::size_t>(w) * h * 3);
    const int barW = w / 8;
    // 8 条彩条（BGR）
    static const uint8_t bars[8][3] = {
        {255, 255, 255}, {0, 255, 255}, {255, 255, 0}, {0, 255, 0},
        {255, 0, 255}, {0, 0, 255}, {255, 0, 0}, {64, 64, 64}};
    // 移动方块的位置随帧号变化，便于确认「画面在动」且能对上帧号
    const int boxW = w / 8, boxH = h / 8;
    const int boxX = (frame * 7) % (w - boxW);
    const int boxY = (frame * 3) % (h - boxH);

    for (int y = 0; y < h; ++y) {
        uint8_t *row = buf.data() + static_cast<std::size_t>(y) * w * 3;
        for (int x = 0; x < w; ++x) {
            const uint8_t *c = bars[(x / barW) % 8];
            // 上半部彩条，下半部加纵向渐变，方便看画质损失
            const int dim = (y > h / 2) ? (255 - (y - h / 2) * 255 / (h / 2)) : 255;
            uint8_t *p = row + x * 3;
            p[0] = static_cast<uint8_t>(c[0] * dim / 255);
            p[1] = static_cast<uint8_t>(c[1] * dim / 255);
            p[2] = static_cast<uint8_t>(c[2] * dim / 255);
        }
    }
    // 白色移动方块
    for (int y = boxY; y < boxY + boxH && y < h; ++y) {
        uint8_t *row = buf.data() + static_cast<std::size_t>(y) * w * 3;
        for (int x = boxX; x < boxX + boxW && x < w; ++x) {
            uint8_t *p = row + x * 3;
            p[0] = p[1] = p[2] = 255;
        }
    }
}

void usage(const char *a) {
    std::printf(
        "RM2027 图传发送工具（合成图案 -> HEVC 编码 -> UDP）\n\n用法: %s [选项]\n\n"
        "  --host <ip>        目标地址，默认 127.0.0.1\n"
        "  --port <n>         目标端口，默认 3334\n"
        "  --width/--height   编码分辨率，默认 1920x1080（官方规格）\n"
        "  --fps <n>          帧率，默认 60\n"
        "  --bitrate <kbps>   码率，默认 8000\n"
        "  --duration <sec>   发送时长，0 = 直到 Ctrl-C，默认 0\n"
        "  --gop <n>          关键帧间隔，默认 60；1=全 IDR；-1=只发一个 IDR\n"
        "  --bframes <n>      B 帧数量，默认 0\n"
        "  --slices <n>       每帧 slice 数，默认 1\n"
        "  --aud              插入 AUD\n"
        "  --no-repeat-headers  参数集不随 IDR 重复\n"
        "  --intra-refresh    用 intra-refresh 代替 IDR（丢包后画面自愈，低延迟链路常用）\n"
        "  --pix-fmt <fmt>    默认 yuv420p（可 yuv420p10le/yuv444p/yuv422p）\n"
        "  --preset <p>       x265 preset，默认 ultrafast\n"
        "  --drop-every <n>   每 N 个包丢 1 个（故障注入）\n"
        "  --dup-every <n>    每 N 个包重复发一次（故障注入）\n"
        "  --shuffle          打乱帧内分片顺序（故障注入）\n"
        "  --corrupt-total    故意写错帧总字节数（故障注入）\n"
        "  --rotate <n>       每 N 帧轮换一套编码配置（压力模式）\n"
        "  --mtu <n>          单包上限，默认 1400\n"
        "  --log <level>      debug|info|warn|error\n",
        a);
}
} // namespace

int main(int argc, char **argv) {
    rm2027::VideoSenderOptions opt;
    int durationSec = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char *w) -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "选项 %s 缺少参数\n", w); std::exit(2); }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else if (a == "--host") opt.targetHost = next("--host");
        else if (a == "--port") opt.targetPort = static_cast<uint16_t>(std::stoi(next("--port")));
        else if (a == "--width") opt.encoder.width = std::stoi(next("--width"));
        else if (a == "--height") opt.encoder.height = std::stoi(next("--height"));
        else if (a == "--fps") opt.encoder.fps = std::stoi(next("--fps"));
        else if (a == "--bitrate") opt.encoder.bitrateKbps = std::stoi(next("--bitrate"));
        else if (a == "--gop") opt.encoder.gopSize = std::stoi(next("--gop"));
        else if (a == "--bframes") { opt.encoder.bFrames = std::stoi(next("--bframes")); opt.encoder.tune.clear(); }
        else if (a == "--slices") opt.encoder.slices = std::stoi(next("--slices"));
        else if (a == "--aud") opt.encoder.aud = true;
        else if (a == "--intra-refresh") opt.encoder.intraRefresh = true;
        else if (a == "--no-repeat-headers") opt.encoder.repeatHeaders = false;
        else if (a == "--pix-fmt") opt.encoder.pixFmt = next("--pix-fmt");
        else if (a == "--preset") opt.encoder.preset = next("--preset");
        else if (a == "--drop-every") opt.dropEveryNthPacket = std::stoi(next("--drop-every"));
        else if (a == "--dup-every") opt.duplicateEveryNthPacket = std::stoi(next("--dup-every"));
        else if (a == "--shuffle") opt.shuffleFragments = true;
        else if (a == "--corrupt-total") opt.corruptFrameTotal = true;
        else if (a == "--rotate") opt.rotateEncoderEveryNFrames = std::stoi(next("--rotate"));
        else if (a == "--mtu") opt.maxDatagram = static_cast<std::size_t>(std::stoul(next("--mtu")));
        else if (a == "--duration") durationSec = std::stoi(next("--duration"));
        else if (a == "--log") {
            const std::string lv = next("--log");
            if (lv == "debug") rm2027::setLogLevel(rm2027::LogLevel::Debug);
            else if (lv == "warn") rm2027::setLogLevel(rm2027::LogLevel::Warn);
            else if (lv == "error") rm2027::setLogLevel(rm2027::LogLevel::Error);
        } else { std::fprintf(stderr, "未知选项: %s\n", a.c_str()); usage(argv[0]); return 2; }
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    rm2027::VideoSender tx(opt);
    g_tx = &tx;
    std::string err;
    if (!tx.start(&err)) { RM_ERROR("main", err); return 1; }

    const int w = opt.encoder.width, h = opt.encoder.height;
    std::vector<uint8_t> buf;
    const auto interval = std::chrono::microseconds(1000000 / (opt.encoder.fps > 0 ? opt.encoder.fps : 60));
    const auto t0 = std::chrono::steady_clock::now();
    int frame = 0;
    while (tx.running()) {
        makePattern(buf, w, h, frame);
        std::string e;
        if (!tx.sendFrame(buf.data(), w, h, w * 3, &e)) {
            RM_ERROR("main", "发送失败: " << e);
        }
        ++frame;
        if (durationSec > 0 &&
            std::chrono::steady_clock::now() - t0 >= std::chrono::seconds(durationSec)) break;
        const auto elapsed = std::chrono::steady_clock::now() - t0;
        const auto target = interval * frame;
        if (elapsed < target) std::this_thread::sleep_for(target - elapsed);
    }

    tx.flush(nullptr);
    const auto s = tx.stats();
    std::printf("%s\n", rm2027::videoStatsToJson(s).c_str());
    tx.stop();
    return 0;
}
