// RM2027 —— 最小 MQTT 3.1.1 broker
//
// 为什么自己写：官方协议里「服务器」就是 192.168.12.1:3333 的 MQTT broker。
// 为了让 C++ 服务器自成一体（不依赖 mosquitto/aedes），这里实现覆盖测试所需的最小子集：
//   CONNECT/CONNACK, PUBLISH(QoS 0/1/2), PUBACK/PUBREC/PUBREL/PUBCOMP,
//   SUBSCRIBE/SUBACK, UNSUBSCRIBE/UNSUBACK, PINGREQ/PINGRESP, DISCONNECT
//   + 通配符 topic 匹配（+/#）+ retain + keepalive 超时清理
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rm2027 {

struct MqttMessage {
    std::string topic;
    std::string payload;
    int qos{0};
    bool retain{false};
};

class MqttBroker {
public:
    using ClientHandler = std::function<void(const std::string &clientId)>;
    using PublishHandler =
        std::function<void(const std::string &clientId, const MqttMessage &msg)>;

    MqttBroker();
    ~MqttBroker();

    MqttBroker(const MqttBroker &) = delete;
    MqttBroker &operator=(const MqttBroker &) = delete;

    // 监听；host 可为 "0.0.0.0"。成功返回 true
    bool listen(const std::string &host, uint16_t port, std::string *error);
    void stop();

    // 服务器侧主动发布（clientId 为空表示非客户端来源）
    void publish(const MqttMessage &msg);

    void onClientConnected(ClientHandler h) { onConnect_ = std::move(h); }
    void onClientDisconnected(ClientHandler h) { onDisconnect_ = std::move(h); }
    void onClientPublish(PublishHandler h) { onPublish_ = std::move(h); }

    std::size_t clientCount() const;
    // 已经至少有一条订阅的客户端数（用于判断订阅是否就绪，避免发早了丢消息）
    std::size_t subscribedClientCount() const;
    // 订阅了指定 topic 的客户端数（用于判断「要发的这个 topic」是否就绪）
    std::size_t topicSubscriberCount(const std::string &topic) const;
    std::vector<std::string> clientIds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ClientHandler onConnect_;
    ClientHandler onDisconnect_;
    PublishHandler onPublish_;
};

// topic 通配符匹配（暴露出来便于单测）
bool topicMatches(const std::string &filter, const std::string &topic);

} // namespace rm2027
