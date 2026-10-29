// RM2027 自定义终端协议 V1.0.0 —— 客户端对外接口
//
// 用法概览：
//     rm2027::Client client({.host = "192.168.12.1", .port = 3333, .clientId = "3"});
//     client.on<rm2027::GameStatus>([](std::unique_ptr<rm2027::GameStatus> gs) { ... });
//     client.start();
//     auto cmd = std::make_unique<rm2027::DartCommand>();
//     cmd->target_id = 1;  cmd->launch_confirm = true;
//     client.send(std::move(cmd));                       // 名字取自类型
//     client.send("DartCommand", std::move(otherCmd));   // 或显式指定指令名
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>

#include "rm2027/api/message.h"

namespace rm2027 {

class Client {
public:
    // 收到一条指令时回调；参数是移动进来的原生结构体，回调内拥有其所有权
    using Handler = std::function<void(std::unique_ptr<Message>)>;

    struct Options {
        std::string host{"127.0.0.1"};   // 官方：192.168.12.1
        uint16_t port{3333};
        std::string clientId{"1"};       // 官方：所连接机器人对应的 ID 编号
        bool subscribeAllDownlink{true}; // 启动时订阅全部 23 条下行指令
        bool logMessages{true};          // 打到 stderr 的人类可读日志

        // ---- 连接管理（自动重连）----
        // 启动后**一直**在后台尝试连接，直到连上为止；断线后自动重连。
        // 所以 start() 不再因为 broker 没起而失败。
        int connectTimeoutSec{5};     // 单次 connect 的超时
        int connectRetryMs{1000};     // 连接失败后首次重试的退避
        int maxConnectRetryMs{5000};  // 退避上限（指数退避：1s→2s→4s→5s…）
        int firstConnectWaitMs{0};    // start() 最多等多久首次「连接+订阅」再返回；
                                      // 默认 0 = 完全不阻塞（start 立即返回）。
                                      // 需要「返回时已订阅好」的调用方再调大。

        // ---- 发送 ----
        // false（默认）：publish 后**不等待** PUBACK，调用线程不会被网络往返卡住。
        // 对机器人控制这种高频指令，阻塞等待 PUBACK 会让调用方（比如 GUI 主线程）
        // 被卡住，进而连接收显示都跟着停 —— 所以默认非阻塞。
        bool waitForPubAck{false};
    };

    // 连接状态变化回调（在 Paho 回调线程里触发，别在里面阻塞）
    using ConnectionHandler = std::function<void(bool connected)>;

    Client();
    explicit Client(Options opt);
    ~Client();

    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    // 启动后台连接线程并立即返回。连接不上不会失败 —— 后台一直重试。
    // 会最多等 firstConnectWaitMs 尝试完成首次「连接+订阅」（便于调用方紧接着发消息），
    // 但即使没等到也照样返回 true，连接在后台继续。
    bool start(std::string *error = nullptr);
    void stop();
    void requestStop();
    bool isConnected() const;
    void onConnectionChanged(ConnectionHandler cb);

    // ------------------------------------------------------------------
    // 发送（发送方）
    // ------------------------------------------------------------------
    // 由结构体自身的类型决定指令名
    bool send(std::unique_ptr<Message> msg);
    // 显式指定指令名；会校验与结构体类型一致
    bool send(const std::string &name, std::unique_ptr<Message> msg);

    template <typename T, typename = std::enable_if_t<std::is_base_of<Message, T>::value>>
    bool send(std::unique_ptr<T> msg) {
        return send(std::unique_ptr<Message>(msg.release()));
    }

    // ------------------------------------------------------------------
    // 绑定回调（接收方）
    // ------------------------------------------------------------------
    void on(const std::string &name, Handler cb);

    template <typename T, typename = std::enable_if_t<std::is_base_of<Message, T>::value>>
    void on(std::function<void(std::unique_ptr<T>)> cb) {
        on(T::kName, [cb](std::unique_ptr<Message> m) {
            cb(std::unique_ptr<T>(static_cast<T *>(m.release())));
        });
    }

    // 所有收到的指令都会额外触发（在专属回调之后）。可用于日志/落盘/测试。
    void onAny(Handler cb);

    // ------------------------------------------------------------------
    // 运行控制
    // ------------------------------------------------------------------
    void waitFor(std::chrono::milliseconds d); // 阻塞指定时长（可被 requestStop 提前打断）
    void runUntilStopped();                    // 阻塞直到 requestStop/stop

    // 统计
    std::size_t receivedCount() const;
    std::size_t sentCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rm2027
