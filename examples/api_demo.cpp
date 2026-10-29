// ===========================================================================
// RM2027 对外 API 最小示例
//
// 外部使用者只需要包含三个头文件：
//     rm2027/api/messages.h   —— 32 条指令（上行 9 / 下行 23）对应的 C++ 原生结构体
//     rm2027/api/client.h     —— 客户端接口
//     rm2027/api/server.h     —— 服务器接口
//
// 发送：指定指令名（或直接给结构体），传入 std::unique_ptr<结构体>（移动语义）
// 接收：按指令名绑定回调，回调参数同样是 std::unique_ptr<结构体>
//
// 编译（见 CMakeLists 的 rm_api_demo 目标）：
//     g++ -std=c++17 examples/api_demo.cpp -lrm2027_api_server -lrm2027_api_client ...
// ===========================================================================
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include <rm2027/api/client.h>
#include <rm2027/api/messages.h>
#include <rm2027/api/server.h>

namespace {

// --------------------------------------------------------------------------
// 服务器侧
// --------------------------------------------------------------------------
int runServer(uint16_t port) {
    rm2027::Server::Options opt;
    opt.host = "127.0.0.1";
    opt.port = port;

    rm2027::Server server(opt);

    // ---- 接收方：按【结构体类型】绑定回调（类型安全，推荐）----
    server.on<rm2027::DartCommand>([](std::unique_ptr<rm2027::DartCommand> cmd) {
        std::printf("[server] DartCommand        target_id=%u open=%d launch_confirm=%d\n",
                    cmd->target_id, cmd->open ? 1 : 0, cmd->launch_confirm ? 1 : 0);
        std::fflush(stdout);
    });

    // ---- 接收方：按【指令名】绑定，回调拿到基类指针，自行向下转型 ----
    server.on("CommonCommand", [](std::unique_ptr<rm2027::Message> m) {
        const auto *c = static_cast<const rm2027::CommonCommand *>(m.get());
        std::printf("[server] CommonCommand      cmd_type=%u param=%u\n", c->cmd_type, c->param);
        std::fflush(stdout);
    });

    // ---- 接收方：不限指令名，收到什么都触发（可拿 clientId）----
    server.onAnyFrom([](std::unique_ptr<rm2027::Message> m, const std::string &clientId) {
        std::printf("[server] (any) %s from client=%s\n", m->name(), clientId.c_str());
        std::fflush(stdout);
    });

    std::string err;
    if (!server.start(&err)) {
        std::fprintf(stderr, "服务器启动失败: %s\n", err.c_str());
        return 1;
    }

    // 等客户端把「要发的这两个 topic」订阅好再发。客户端是逐条订阅的，
    // subscribedClientCount()>0 只代表第一个 topic 订好了，不代表 GameStatus /
    // RobotPosition 都订好了 —— 直接发第一条会落在窗口里被丢（曾间歇复现）。
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while ((server.topicSubscriberCount("GameStatus") == 0 ||
                server.topicSubscriberCount("RobotPosition") == 0) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::printf("[server] GameStatus 订阅 %zu 个，RobotPosition 订阅 %zu 个\n",
                    server.topicSubscriberCount("GameStatus"),
                    server.topicSubscriberCount("RobotPosition"));
    }

    // ---- 发送方：构造结构体 -> 按名字发 ----
    for (int round = 0; round < 3; ++round) {
        auto st = std::make_unique<rm2027::GameStatus>();
        st->current_round = 1;
        st->total_rounds = 3;
        st->current_stage = 4;         // 比赛中
        st->red_score = 100 + round;
        st->blue_score = 80;
        st->game_result = 255;         // 官方约定：非结算阶段发 255
        server.send(std::move(st));    // 名字自动取自类型

        auto pos = std::make_unique<rm2027::RobotPosition>();
        pos->x = 1.5f;
        pos->y = -2.5f;
        pos->robot_id = 3;
        server.send("RobotPosition", std::move(pos));   // 也可显式指定指令名

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));   // 等客户端上行
    server.stop();
    std::printf("[server] 统计: 收 %zu 发 %zu\n", server.receivedCount(), server.sentCount());
    return 0;
}

// --------------------------------------------------------------------------
// 客户端侧
// --------------------------------------------------------------------------
int runClient(uint16_t port) {
    rm2027::Client::Options opt;
    opt.host = "127.0.0.1";
    opt.port = port;
    opt.clientId = "3";   // 官方规定：clientID = 机器人 ID
    // 只订阅下面 on<...> 绑定的两个 topic（而不是全部 23 条下行），
    // 这样服务端能用 topicSubscriberCount 精确判断就绪。
    opt.subscribeAllDownlink = false;

    rm2027::Client client(opt);

    // ---- 接收方：按结构体类型绑定 ----
    client.on<rm2027::GameStatus>([](std::unique_ptr<rm2027::GameStatus> st) {
        std::printf("[client] GameStatus         stage=%u red=%u blue=%u result=%u\n",
                    st->current_stage, st->red_score, st->blue_score, st->game_result);
        std::fflush(stdout);
    });
    client.on<rm2027::RobotPosition>([](std::unique_ptr<rm2027::RobotPosition> pos) {
        std::printf("[client] RobotPosition      robot_id=%u x=%.1f y=%.1f\n", pos->robot_id,
                    pos->x, pos->y);
        std::fflush(stdout);
    });

    std::string err;
    if (!client.start(&err)) {
        std::fprintf(stderr, "客户端连接失败: %s\n", err.c_str());
        return 1;
    }

    // start() 是异步连接（立即返回），发送前先等它真正连上，
    // 否则这几条上行会落在「还没连上」的窗口里被丢弃。
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!client.isConnected() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // ---- 发送方：按指令名发 ----
    auto dart = std::make_unique<rm2027::DartCommand>();
    dart->target_id = 1;          // 前哨站
    dart->open = true;
    dart->launch_confirm = true;
    client.send(std::move(dart));

    auto common = std::make_unique<rm2027::CommonCommand>();
    common->cmd_type = 1;         // 兑换 17mm 发弹量
    common->param = 20;
    client.send("CommonCommand", std::move(common));

    client.waitFor(std::chrono::milliseconds(2500));
    client.stop();
    std::printf("[client] 统计: 收 %zu 发 %zu\n", client.receivedCount(), client.sentCount());
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    const uint16_t port = (argc > 2) ? static_cast<uint16_t>(std::stoi(argv[2])) : 13500;
    const std::string mode = (argc > 1) ? argv[1] : "help";

    if (mode == "server") return runServer(port);
    if (mode == "client") return runClient(port);

    std::printf("用法: %s <server|client> [port]\n", argv[0]);
    return 2;
}
