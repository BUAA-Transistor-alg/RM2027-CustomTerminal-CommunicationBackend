// RM2027 协议服务器命令行工具 —— 完全基于对外接口 rm2027::Server 实现
//
// 用法示例：
//   rm_server --host 192.168.12.1 --port 3333 --scenario tests/scenarios/all_down.json
//   （图传已改为由 Python/GUI 驱动：C++ 侧只提供 VideoSender/VideoReceiver）
#include <chrono>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <google/protobuf/util/json_util.h>

#include "rm2027/api/messages.h"
#include "rm2027/api/message.h"
#include "rm2027/api/scenario.h"
#include "rm2027/api/wire_format.h"
#include "rm2027/api/server.h"
#include "rm2027/common/log.h"
#include "rm2027_scenario.pb.h"

namespace {

rm2027::Server *g_server = nullptr;

void onSignal(int) {
    if (g_server) g_server->requestStop();
}

// 服务器是否还在运行（信号处理里会 requestStop）
bool g_running() { return g_server != nullptr && g_server->isRunning(); }

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void emitRecord(const char *dir, const std::string &topic, std::size_t size,
                const std::string &json, const std::string &extra = std::string()) {
    std::printf("{\"ts\":\"%s\",\"dir\":\"%s\",\"topic\":\"%s\",\"size\":%zu,\"data\":%s%s}\n",
                rm2027::nowIso8601().c_str(), dir, topic.c_str(), size, json.c_str(),
                extra.c_str());
    std::fflush(stdout);
}

void emitEvent(const char *ev, const std::string &extra) {
    std::printf("{\"ts\":\"%s\",\"ev\":\"%s\"%s}\n", rm2027::nowIso8601().c_str(), ev,
                extra.c_str());
    std::fflush(stdout);
}

void printCatalog() {
    std::printf("%-40s %-6s\n", "指令 (MQTT topic)", "方向");
    std::printf("%s\n", std::string(48, '-').c_str());
    for (const auto &n : rm2027::allMessageNames()) {
        std::printf("%-40s %-6s\n", n.c_str(),
                    rm2027::directionName(rm2027::messageDirection(n)));
    }
    std::printf("\n共 %zu 条指令（官方协议 V1.0.0）\n", rm2027::allMessageNames().size());
}

void usage(const char *argv0) {
    std::printf(
        "RM2027 C++ 协议服务器 (官方协议 V1.0.0, 自带最小 MQTT 3.1.1 broker)\n"
        "\n用法: %s [选项]\n"
        "\n选项:\n"
        "  --host <ip>              监听地址，默认 0.0.0.0\n"
        "                           比赛现场官方服务器为 192.168.12.1（客户端固定 192.168.12.2）\n"
        "  --port <n>               监听端口，默认 3333（官方端口）\n"
        "  --scenario <file.json>   测试场景：按确定的值发布下行指令\n"
        "  --repeat <n>             场景重复次数，0 = 无限，默认 1\n"
        "  --interval <ms>          步骤间隔，默认 300\n"
        "  --start-after <n>        至少 n 个客户端连上后开始发布，默认 1\n"
        "  --wait-clients <ms>      等待客户端超时，默认 15000\n"
        "  --linger <ms>            场景发完后的驻留时间，默认 8000，0 = 立即退出\n"
        "  --list                   列出全部指令后退出\n"
        "  --radar-wire-format <nested|flat>  RadarInfoToClient 线格式，默认 nested\n"
        "                           nested = 嵌套 message（社区实现主流）；\n"
        "                           flat   = 官方 PDF §2.2.19 字面 packed uint32（24 个值）\n"
        "  --log <debug|info|warn|error>  日志等级，默认 info\n"
        "  -h, --help               显示帮助\n",
        argv0);
}

} // namespace

int main(int argc, char **argv) {
    rm2027::Server::Options opt;
    std::string scenarioFile;
    int repeat = 1, intervalMs = 300, startAfter = 1, waitClientsMs = 15000, lingerMs = 8000;
    bool listOnly = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char *what) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "选项 %s 缺少参数\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else if (a == "--list") listOnly = true;
        else if (a == "--host") opt.host = next("--host");
        else if (a == "--port") opt.port = static_cast<uint16_t>(std::stoi(next("--port")));
        else if (a == "--scenario") scenarioFile = next("--scenario");
        else if (a == "--repeat") repeat = std::stoi(next("--repeat"));
        else if (a == "--interval") intervalMs = std::stoi(next("--interval"));
        else if (a == "--start-after") startAfter = std::stoi(next("--start-after"));
        else if (a == "--wait-clients") waitClientsMs = std::stoi(next("--wait-clients"));
        else if (a == "--linger") lingerMs = std::stoi(next("--linger"));
        else if (a == "--radar-wire-format") {
            const std::string v = next("--radar-wire-format");
            rm2027::RadarWireFormat fmt;
            if (!rm2027::parseRadarWireFormat(v.c_str(), &fmt)) {
                std::fprintf(stderr, "无效的 --radar-wire-format: %s（可选 nested / flat）\n",
                             v.c_str());
                return 2;
            }
            rm2027::setRadarWireFormat(fmt);
        }
        else if (a == "--log") {
            const std::string lv = next("--log");
            if (lv == "debug") rm2027::setLogLevel(rm2027::LogLevel::Debug);
            else if (lv == "info") rm2027::setLogLevel(rm2027::LogLevel::Info);
            else if (lv == "warn") rm2027::setLogLevel(rm2027::LogLevel::Warn);
            else if (lv == "error") rm2027::setLogLevel(rm2027::LogLevel::Error);
        } else {
            std::fprintf(stderr, "未知选项: %s\n", a.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    if (listOnly) {
        printCatalog();
        return 0;
    }

    rm2027::Server server(opt);
    g_server = &server;

    // 接收方：上行指令回调（附带发送方 clientID）
    server.onAnyFrom([](std::unique_ptr<rm2027::Message> msg, const std::string &clientId) {
        const std::string json = rm2027::toJson(*msg);
        emitRecord("up", msg->name(), rm2027::toBinary(*msg).size(), json,
                   ",\"clientId\":\"" + clientId + "\"");
    });

    server.onClientConnected([](const std::string &id) {
        emitEvent("client-connected", ",\"clientId\":\"" + id + "\"");
    });
    server.onClientDisconnected([](const std::string &id) {
        emitEvent("client-disconnected", ",\"clientId\":\"" + id + "\"");
    });

    std::string err;
    if (!server.start(&err)) {
        RM_ERROR("main", "服务器启动失败: " << err);
        return 1;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // ---- 场景发布 ----
    if (!scenarioFile.empty()) {
        rm2027::Scenario scenario;
        std::string serr;
        if (!rm2027::Scenario::load(scenarioFile, &scenario, &serr)) {
            RM_ERROR("main", serr);
            server.stop();
            return 1;
        }
        RM_INFO("main", "已加载场景: " << scenario.size() << " 步，间隔 " << intervalMs
                                       << "ms，重复 " << (repeat == 0 ? -1 : repeat) << " 次");

        // 等到足够多的客户端订阅上来（否则最早几帧会丢）
        if (startAfter > 0) {
            const int64_t deadline = nowMs() + waitClientsMs;
            while (server.subscribedClientCount() < static_cast<std::size_t>(startAfter) &&
                   nowMs() < deadline && g_running()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            const std::size_t n = server.subscribedClientCount();
            if (n < static_cast<std::size_t>(startAfter)) {
                RM_WARN("main", "等待客户端超时（当前 " << n << " 个，需要 " << startAfter << "）");
            } else {
                RM_INFO("main", "已有 " << n << " 个客户端订阅，开始发布");
            }
        }

        int rounds = 0;
        while (g_running() && (repeat == 0 || rounds < repeat)) {
            for (const auto &step : scenario.steps()) {
                if (!g_running()) break;
                std::string e;
                auto msg = rm2027::fromJson(step.topic, step.dataJson, &e);
                if (!msg || !server.send(std::move(msg))) {
                    RM_WARN("main", "发布 " << step.topic << " 失败: " << e);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
            }
            ++rounds;
        }
    } else {
        // 没给场景就只是挂着当 broker 用
        const int64_t deadline = nowMs() + (lingerMs > 0 ? lingerMs : 0);
        while (g_running() && (lingerMs == 0 || nowMs() < deadline)) {
            if (lingerMs == 0) {
                if (!g_running()) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // ---- 驻留，让客户端收完最后几条 ----
    if (lingerMs > 0) {
        const int64_t deadline = nowMs() + lingerMs;
        while (g_running() && nowMs() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    server.stop();
    RM_INFO("main", "统计: 收到 " << server.receivedCount() << " 条, 发送 " << server.sentCount()
                                  << " 条");
    return 0;
}
