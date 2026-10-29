// RM2027 自定义终端协议 V1.0.0 —— 服务器对外接口（自带最小 MQTT broker）
//
// 官方架构里「服务器」就是 MQTT broker 本身，所以本接口直接监听端口，无需外部 broker。
//
// 用法概览：
//     rm2027::Server server({.host = "192.168.12.1", .port = 3333});
//     server.on<rm2027::DartCommand>([](std::unique_ptr<rm2027::DartCommand> cmd) { ... });
//     server.start();
//     auto st = std::make_unique<rm2027::GameStatus>();
//     st->current_stage = 4;
//     server.send(std::move(st));
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>

#include "rm2027/api/message.h"

namespace rm2027 {

class Server {
public:
    // 收到一条上行指令；参数是移动进来的原生结构体，回调内拥有其所有权
    using Handler = std::function<void(std::unique_ptr<Message>)>;
    // 同上，但额外告知发送方 clientID（官方规定 clientID = 机器人 ID）
    using FromHandler = std::function<void(std::unique_ptr<Message>, const std::string &clientId)>;

    struct Options {
        std::string host{"0.0.0.0"}; // 官方：192.168.12.1
        uint16_t port{3333};
        bool logMessages{true};
    };

    Server();
    explicit Server(Options opt);
    ~Server();

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    bool start(std::string *error = nullptr);
    void stop();
    void requestStop();
    bool isRunning() const;

    // ------------------------------------------------------------------
    // 发送（发送方）：发布下行指令
    // ------------------------------------------------------------------
    bool send(std::unique_ptr<Message> msg);
    bool send(const std::string &name, std::unique_ptr<Message> msg);

    template <typename T, typename = std::enable_if_t<std::is_base_of<Message, T>::value>>
    bool send(std::unique_ptr<T> msg) {
        return send(std::unique_ptr<Message>(msg.release()));
    }

    // ------------------------------------------------------------------
    // 绑定回调（接收方）：接收上行指令
    // ------------------------------------------------------------------
    void on(const std::string &name, Handler cb);
    void onFrom(const std::string &name, FromHandler cb);

    template <typename T, typename = std::enable_if_t<std::is_base_of<Message, T>::value>>
    void on(std::function<void(std::unique_ptr<T>)> cb) {
        on(T::kName, [cb](std::unique_ptr<Message> m) {
            cb(std::unique_ptr<T>(static_cast<T *>(m.release())));
        });
    }

    // 所有收到的指令都会额外触发（在专属回调之后）
    void onAny(Handler cb);
    void onAnyFrom(FromHandler cb);

    // 客户端上下线通知（可选）
    void onClientConnected(std::function<void(const std::string &clientId)> cb);
    void onClientDisconnected(std::function<void(const std::string &clientId)> cb);

    // ------------------------------------------------------------------
    // 运行控制 / 状态
    // ------------------------------------------------------------------
    void runUntilStopped(); // 阻塞直到 requestStop/stop
    std::size_t clientCount() const;
    // 已订阅（至少一条）的客户端数
    std::size_t subscribedClientCount() const;
    // 订阅了指定 topic 的客户端数（判断「要发的这个 topic」是否就绪）
    std::size_t topicSubscriberCount(const std::string &topic) const;
    std::size_t receivedCount() const;
    std::size_t sentCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rm2027
