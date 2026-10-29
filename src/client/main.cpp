// RM2027 自定义终端命令行工具 —— 完全基于对外接口 rm2027::Client 实现
//
// 用法示例：
//   rm_client --host 192.168.12.1 --port 3333 --client-id 3 --subscribe all --duration 10
//   rm_client --publish-scenario tests/scenarios/all_up.json
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <google/protobuf/util/json_util.h>

#include "rm2027/api/client.h"
#include "rm2027/api/message.h"
#include "rm2027/api/scenario.h"
#include "rm2027/api/messages.h"
#include "rm2027/common/log.h"
#include "rm2027_scenario.pb.h"

namespace {

rm2027::Client *g_client = nullptr;
std::atomic<bool> g_stop{false};

void onSignal(int) {
    g_stop.store(true);
    if (g_client) g_client->requestStop();
}

// 客户端是否还在运行。**不能**用 isConnected()：现在 start() 是异步连接、
// 启动瞬间还没连上，用 isConnected() 会让主循环在连接建立前就退出。
bool g_running() { return !g_stop.load(); }

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::vector<std::string> splitCsv(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// 输出一行结构化记录（stdout），供自动化验证脚本解析
void emitRecord(const char *dir, const std::string &topic, std::size_t size,
                const std::string &json) {
    std::printf("{\"ts\":\"%s\",\"dir\":\"%s\",\"topic\":\"%s\",\"size\":%zu,\"data\":%s}\n",
                rm2027::nowIso8601().c_str(), dir, topic.c_str(), size, json.c_str());
    std::fflush(stdout);
}

void usage(const char *argv0) {
    std::printf(
        "RM2027 C++ 自定义终端 (官方协议 V1.0.0)\n"
        "\n用法: %s [选项]\n"
        "\n选项:\n"
        "  --host <ip>               broker 地址，默认 127.0.0.1（官方 192.168.12.1）\n"
        "  --port <n>                broker 端口，默认 3333\n"
        "  --client-id <id>          MQTT clientID = 机器人 ID，默认 1\n"
        "                            （红方：1重装 3/4/5步兵 6空中系统 7哨兵；蓝方 = 红方 +100，2/102 为保留位）\n"
        "  --subscribe <what>        all | none | <指令名,...>，默认 all（订阅全部 23 条下行）\n"
        "  --publish-scenario <f>    发送该场景文件里的上行指令\n"
        "  --publish-interval <ms>   上行发送间隔，默认 100\n"
        "  --duration <sec>          运行指定秒数后退出，0 = 一直运行，默认 0\n"
        "  --log <debug|info|warn|error>  日志等级，默认 info（日志走 stderr）\n"
        "  -h, --help               显示帮助\n"
        "\n说明: stdout 只输出结构化 JSON 记录，人类可读日志在 stderr。\n",
        argv0);
}

} // namespace

int main(int argc, char **argv) {
    g_stop.store(false);
    rm2027::Client::Options opt;
    std::string subscribeSpec = "all";
    std::string publishScenario;
    int publishIntervalMs = 100;
    int durationSec = 0;

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
        else if (a == "--host") opt.host = next("--host");
        else if (a == "--port") opt.port = static_cast<uint16_t>(std::stoi(next("--port")));
        else if (a == "--client-id") opt.clientId = next("--client-id");
        else if (a == "--subscribe") subscribeSpec = next("--subscribe");
        else if (a == "--publish-scenario") publishScenario = next("--publish-scenario");
        else if (a == "--publish-interval") publishIntervalMs = std::stoi(next("--publish-interval"));
        else if (a == "--duration") durationSec = std::stoi(next("--duration"));
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

    opt.subscribeAllDownlink = (subscribeSpec == "all");

    rm2027::Client client(opt);
    g_client = &client;

    // 接收方：所有收到的下行指令都打一条记录（这就是「绑定回调」的用法）
    client.onAny([](std::unique_ptr<rm2027::Message> msg) {
        const std::string json = rm2027::toJson(*msg);
        emitRecord("down", msg->name(), rm2027::toBinary(*msg).size(), json);
    });

    // 指定子集时逐个绑定；同时演示类型安全的 on<T>
    if (subscribeSpec != "all" && subscribeSpec != "none") {
        for (const auto &t : splitCsv(subscribeSpec)) {
            if (!rm2027::isKnownMessage(t)) {
                std::fprintf(stderr, "未知指令: %s\n", t.c_str());
                return 2;
            }
            client.on(t, [](std::unique_ptr<rm2027::Message>) {});
        }
    }

    std::string err;
    if (!client.start(&err)) {
        RM_ERROR("main", "客户端启动失败: " << err);
        return 1;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // ---- 按场景发上行指令；没有场景就单纯接收 ----
    rm2027::Scenario scenario;
    if (!publishScenario.empty()) {
        std::string serr;
        if (!rm2027::Scenario::load(publishScenario, &scenario, &serr)) {
            RM_ERROR("main", serr);
            client.stop();
            return 1;
        }
        RM_INFO("main", "已加载上行场景: " << scenario.size() << " 步，间隔 "
                                          << publishIntervalMs << "ms");
    }

    const int64_t t0 = nowMs();
    std::size_t step = 0;
    int64_t nextPublish = nowMs();
    while (g_running()) {
        const int64_t now = nowMs();
        if (durationSec > 0 && now - t0 >= static_cast<int64_t>(durationSec) * 1000) break;

        if (!scenario.empty() && now >= nextPublish) {
            const auto &st = scenario.steps()[step % scenario.size()];
            std::string e;
            auto msg = rm2027::fromJson(st.topic, st.dataJson, &e);
            if (!msg) {
                RM_WARN("main", "解析 " << st.topic << " 失败: " << e);
            } else {
                // 先留一份内容再 move —— 这条记录供自动化校验比对
                const std::string json = rm2027::toJson(*msg);
                const std::size_t sz = rm2027::toBinary(*msg).size();
                if (client.send(std::move(msg))) {
                    emitRecord("up", st.topic, sz, json);   // 记录里方向标 up（校验脚本按 dir 取）
                } else {
                    RM_WARN("main", "发送 " << st.topic << " 失败");
                }
            }
            ++step;
            nextPublish = now + (publishIntervalMs > 0 ? publishIntervalMs : 100);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    client.stop();
    std::printf("{\"ts\":\"%s\",\"ev\":\"summary\",\"received\":%zu,\"published\":%zu}\n",
                rm2027::nowIso8601().c_str(), client.receivedCount(), client.sentCount());
    return 0;
}
