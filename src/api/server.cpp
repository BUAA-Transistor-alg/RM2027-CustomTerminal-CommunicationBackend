// RM2027 服务器对外接口 —— 基于自带最小 MQTT broker 的实现
#include "rm2027/api/server.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>

#include "rm2027/api/detail/registry.h"
#include "rm2027/api/validate.h"
#include "rm2027/broker/mqtt_broker.h"
#include "rm2027/common/log.h"
#include "rm2027/protocol/codec.h"

namespace rm2027 {

struct Server::Impl {
    Options opt;
    MqttBroker broker;

    std::atomic<bool> running{false};
    std::atomic<std::size_t> received{0};
    std::atomic<std::size_t> sent{0};
    std::mutex mutex;
    std::condition_variable cv;

    std::mutex handlerMutex;
    std::map<std::string, Handler> handlers;
    std::map<std::string, FromHandler> fromHandlers;
    Handler any;
    FromHandler anyFrom;
    std::function<void(const std::string &)> onConnected;
    std::function<void(const std::string &)> onDisconnected;

    void handlePublish(const std::string &clientId, const MqttMessage &mqttMsg) {
        const MessageDesc *desc = findMessage(mqttMsg.topic);
        if (desc == nullptr) {
            RM_WARN("server", "收到未知指令: " << mqttMsg.topic << " (来自 " << clientId << ")");
            return;
        }
        std::string err;
        auto native = fromWire(*desc, mqttMsg.payload.data(), mqttMsg.payload.size(), &err);
        if (!native) {
            RM_WARN("server", "解码失败 " << mqttMsg.topic << ": " << err);
            return;
        }
        received.fetch_add(1);
        if (opt.logMessages) {
            RM_INFO("server", "↑ " << mqttMsg.topic << " (" << mqttMsg.payload.size() << " B) 来自 "
                                   << clientId);
        }

        Handler h;
        FromHandler fh;
        Handler a;
        FromHandler af;
        {
            std::lock_guard<std::mutex> lk(handlerMutex);
            auto it = handlers.find(mqttMsg.topic);
            if (it != handlers.end()) h = it->second;
            auto itf = fromHandlers.find(mqttMsg.topic);
            if (itf != fromHandlers.end()) fh = itf->second;
            a = any;
            af = anyFrom;
        }
        const std::string name = mqttMsg.topic;
        if (fh) fh(std::move(native), clientId);
        else if (h) h(std::move(native));
        else if (af || a) {
            if (af) af(std::move(native), clientId);
            else a(std::move(native));
        } else {
            RM_WARN("server", "未绑定回调，丢弃上行指令: " << name);
        }
    }
};

Server::Server() : Server(Options{}) {}

Server::Server(Options opt)
    : impl_(std::make_unique<Impl>()) {
    impl_->opt = std::move(opt);
}

Server::~Server() { stop(); }

bool Server::start(std::string *error) {
    if (!impl_->broker.listen(impl_->opt.host, impl_->opt.port, error)) return false;

    impl_->broker.onClientConnected([this](const std::string &id) {
        RM_INFO("server", "客户端上线: " << id);
        std::function<void(const std::string &)> cb;
        {
            std::lock_guard<std::mutex> lk(impl_->handlerMutex);
            cb = impl_->onConnected;
        }
        if (cb) cb(id);
        impl_->cv.notify_all();
    });
    impl_->broker.onClientDisconnected([this](const std::string &id) {
        RM_INFO("server", "客户端下线: " << id);
        std::function<void(const std::string &)> cb;
        {
            std::lock_guard<std::mutex> lk(impl_->handlerMutex);
            cb = impl_->onDisconnected;
        }
        if (cb) cb(id);
    });
    impl_->broker.onClientPublish(
        [this](const std::string &id, const MqttMessage &m) { impl_->handlePublish(id, m); });

    impl_->running.store(true);
    RM_INFO("server", "服务器已启动: " << impl_->opt.host << ":" << impl_->opt.port);
    return true;
}

void Server::stop() {
    if (!impl_) return;
    if (!impl_->running.exchange(false)) return;
    impl_->broker.stop();
    impl_->cv.notify_all();
}

void Server::requestStop() {
    impl_->running.store(false);
    impl_->cv.notify_all();
}

bool Server::isRunning() const { return impl_->running.load(); }

bool Server::send(std::unique_ptr<Message> msg) {
    if (!msg) {
        RM_ERROR("server", "send: 空指针");
        return false;
    }
    // 同上：先取名再 move，避免依赖参数求值顺序
    const std::string name = msg->name();
    return send(name, std::move(msg));
}

bool Server::send(const std::string &name, std::unique_ptr<Message> msg) {
    if (!msg) {
        RM_ERROR("server", "send(" << name << "): 空指针");
        return false;
    }
    if (name != msg->name()) {
        RM_ERROR("server", "send: 指令名 '" << name << "' 与结构体类型 '" << msg->name()
                                           << "' 不一致");
        return false;
    }
    const MessageDesc *desc = findMessage(name);
    if (desc == nullptr) {
        RM_ERROR("server", "send: 未知指令 " << name);
        return false;
    }
    std::string verr;
    if (!validateMessage(*msg, &verr)) {
        RM_ERROR("server", "send: 指令不符合官方协议约束 " << name << ": " << verr);
        return false;
    }
    std::string payload;
    if (!desc->encode(*msg, &payload)) {
        RM_ERROR("server", "send: 序列化失败 " << name);
        return false;
    }
    MqttMessage m;
    m.topic = name;
    m.payload = payload;
    m.qos = 1;
    impl_->broker.publish(m);
    impl_->sent.fetch_add(1);
    if (impl_->opt.logMessages) {
        RM_INFO("server", "↓ " << name << " (" << payload.size() << " B)");
    }
    return true;
}

void Server::on(const std::string &name, Handler cb) {
    if (!isKnownMessage(name)) {
        RM_ERROR("server", "on: 未知指令 " << name);
        return;
    }
    std::lock_guard<std::mutex> lk(impl_->handlerMutex);
    impl_->handlers[name] = std::move(cb);
}

void Server::onFrom(const std::string &name, FromHandler cb) {
    if (!isKnownMessage(name)) {
        RM_ERROR("server", "onFrom: 未知指令 " << name);
        return;
    }
    std::lock_guard<std::mutex> lk(impl_->handlerMutex);
    impl_->fromHandlers[name] = std::move(cb);
}

void Server::onAny(Handler cb) {
    std::lock_guard<std::mutex> lk(impl_->handlerMutex);
    impl_->any = std::move(cb);
}

void Server::onAnyFrom(FromHandler cb) {
    std::lock_guard<std::mutex> lk(impl_->handlerMutex);
    impl_->anyFrom = std::move(cb);
}

void Server::onClientConnected(std::function<void(const std::string &)> cb) {
    std::lock_guard<std::mutex> lk(impl_->handlerMutex);
    impl_->onConnected = std::move(cb);
}

void Server::onClientDisconnected(std::function<void(const std::string &)> cb) {
    std::lock_guard<std::mutex> lk(impl_->handlerMutex);
    impl_->onDisconnected = std::move(cb);
}

void Server::runUntilStopped() {
    std::unique_lock<std::mutex> lk(impl_->mutex);
    impl_->cv.wait(lk, [this] { return !impl_->running.load(); });
}

std::size_t Server::clientCount() const { return impl_->broker.clientCount(); }

std::size_t Server::subscribedClientCount() const {
    return impl_->broker.subscribedClientCount();
}
std::size_t Server::topicSubscriberCount(const std::string &topic) const {
    return impl_->broker.topicSubscriberCount(topic);
}
std::size_t Server::receivedCount() const { return impl_->received.load(); }
std::size_t Server::sentCount() const { return impl_->sent.load(); }

} // namespace rm2027
