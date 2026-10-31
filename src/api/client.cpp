// RM2027 客户端对外接口 —— 基于 Paho MQTT C++ 的实现（Paho 细节全部隐藏在本文件内）
//
// 连接策略（重要）：
//   * 不用 Paho 的 automatic_reconnect，而是自己在一条「连接线程」里串行处理
//     「连接 -> 订阅 -> 等待断线 -> 重连」。初始连不上会一直重试，断线后也必定
//     重连并**重新订阅**（clean_session=true 时 broker 会清掉订阅）。
//   * 订阅失败会一直重试，直到成功或断线，不会「三次就放弃」。
//   * stop() 之后若还收到迟到的 CONNACK，connected() 里会立即再断开。
#include "rm2027/api/client.h"

#include <mqtt/async_client.h>
#include <mqtt/callback.h>
#include <mqtt/connect_options.h>
#include <mqtt/message.h>
#include <mqtt/token.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "rm2027/api/detail/registry.h"
#include "rm2027/api/validate.h"
#include "rm2027/common/log.h"
#include "rm2027/protocol/codec.h"

namespace rm2027 {

namespace {

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// 把消息派发给注册的回调
class Dispatcher {
public:
    void setHandler(const std::string &name, Client::Handler cb) {
        std::lock_guard<std::mutex> lk(mutex_);
        handlers_[name] = std::move(cb);
    }
    void setAny(Client::Handler cb) {
        std::lock_guard<std::mutex> lk(mutex_);
        any_ = std::move(cb);
    }
    bool has(const std::string &name) const {
        std::lock_guard<std::mutex> lk(mutex_);
        return handlers_.count(name) > 0 || any_ != nullptr;
    }
    std::vector<std::string> names() const {
        std::lock_guard<std::mutex> lk(mutex_);
        std::vector<std::string> v;
        v.reserve(handlers_.size());
        for (auto &kv : handlers_) v.push_back(kv.first);
        return v;
    }
    void dispatch(const std::string &name, std::unique_ptr<Message> msg) {
        Client::Handler h, any;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            auto it = handlers_.find(name);
            if (it != handlers_.end()) h = it->second;
            any = any_;
        }
        if (h) h(std::move(msg));
        else if (any) any(std::move(msg));
        else if (msg) RM_WARN("client", "未绑定回调，丢弃指令: " << name);
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, Client::Handler> handlers_;
    Client::Handler any_;
};
} // namespace

struct Client::Impl : public mqtt::callback {
    Options opt;
    std::unique_ptr<mqtt::async_client> client;
    Dispatcher dispatcher;

    // 这些 atomic 的**写**在正常路径里都持 mutex 再改（保证 cv 不丢唤醒）。
    // 唯一的例外是 requestStop()（信号处理里调用，不能加锁）。
    std::atomic<bool> running{false};
    std::atomic<bool> connectedFlag{false};
    std::atomic<bool> subscribedOnce{false};
    std::atomic<std::size_t> received{0};
    std::atomic<std::size_t> sent{0};
    // 已入队、还没收到 PUBACK 的 QoS1 消息数。用带符号类型，
    // delivery_complete 里做饱和减，避免「同步发送路径不计数」时下溢。
    std::atomic<std::int64_t> inflight{0};

    std::mutex mutex;                       // 保护 cv 谓词依赖的状态写
    std::condition_variable cv;
    std::thread connectThread;

    std::mutex connHandlerMutex;
    Client::ConnectionHandler connectionHandler;

    // ---- 连接线程：串行「连接 -> 订阅 -> 等断线 -> 重连」----
    void connectLoop() {
        int backoff = opt.connectRetryMs;
        while (running.load()) {
            while (running.load() && !connectedFlag.load()) {
                if (!tryConnectOnce()) {
                    RM_WARN("client", "连接失败，" << backoff << "ms 后重试");
                    if (waitRunning(backoff)) return;
                    backoff = std::min(backoff * 2, opt.maxConnectRetryMs);
                    continue;
                }
                backoff = opt.connectRetryMs;
                if (!connectedFlag.load()) {
                    waitPredicate([this] { return connectedFlag.load() || !running.load(); },
                                  1000);
                }
            }
            if (!running.load()) return;

            subscribeAll();
            {
                std::lock_guard<std::mutex> lk(mutex);
                subscribedOnce.store(true);
            }
            cv.notify_all();

            // 等断线（或停止）。**必须**用内部循环：wait_for 超时只是重新检查，
            // 绝不能回到外层 while 重新跑 subscribeAll() —— 否则稳定连接下会每
            // 500ms 重订阅一遍（且 retained 消息被反复投递，已实测复现）。
            while (running.load() && connectedFlag.load()) {
                waitPredicate([this] { return !running.load() || !connectedFlag.load(); }, 500);
            }
        }
    }

    bool tryConnectOnce() {
        mqtt::connect_options conn;
        conn.set_clean_session(true);
        conn.set_keep_alive_interval(20);
        conn.set_connect_timeout(opt.connectTimeoutSec);
        conn.set_automatic_reconnect(false);   // 重连由 connectLoop 自己负责
        conn.set_max_inflight(200);            // 限制 QoS1 未确认消息堆积上限
        try {
            RM_INFO("client", "正在连接 tcp://" << opt.host << ":" << opt.port
                                                  << " (clientId=" << opt.clientId << ")");
            client->connect(conn)->wait_for(std::chrono::seconds(opt.connectTimeoutSec));
            return true;
        } catch (const mqtt::exception &e) {
            RM_WARN("client", "connect 异常: " << e.what());
            return false;
        }
    }

    // 等待指定时长或 running 被置 false；返回 true 表示应停止
    bool waitRunning(int ms) {
        return waitPredicate([this] { return !running.load(); }, ms);
    }

    // 轮询式等待：谓词为真返回 true，超时返回 false。
    // 用短步长反复 wait，即使 notify 丢失（无锁 requestStop 路径）也能在 stepMs 内
    // 重新检查谓词。**每步最多等 stepMs，但最后一步按剩余时间截断**，否则会把
    // waitFor(1ms) 之类短等待强行拉长到 stepMs。
    bool waitPredicate(const std::function<bool()> &pred, int64_t timeoutMs, int64_t stepMs = 100) {
        std::unique_lock<std::mutex> lk(mutex);
        const int64_t deadline = nowMs() + timeoutMs;
        while (!pred() && nowMs() < deadline) {
            const int64_t remaining = deadline - nowMs();
            if (remaining <= 0) break;
            cv.wait_for(lk, std::chrono::milliseconds(std::min(stepMs, remaining)));
        }
        return pred();
    }

    // ---- Paho 回调（绝不能阻塞）----
    void connected(const std::string &cause) override {
        {
            std::lock_guard<std::mutex> lk(mutex);
            connectedFlag.store(true);
        }
        if (!running.load()) {
            // stop() 之后才到的迟到 CONNACK：连接线程已退出，没人会再订阅/使用它，
            // 立即拆掉，避免 stop() 返回后留下一个「幽灵」活跃连接。
            RM_WARN("client", "stop 后收到迟到 CONNACK，立即断开");
            try {
                client->disconnect()->wait_for(std::chrono::seconds(1));
            } catch (const mqtt::exception &) {
            }
            return;
        }
        RM_INFO("client", "已连接 broker (cause: " << (cause.empty() ? "-" : cause) << ")");
        cv.notify_all();
        notifyConn(true);
    }
    void connection_lost(const std::string &cause) override {
        {
            std::lock_guard<std::mutex> lk(mutex);
            connectedFlag.store(false);
        }
        RM_WARN("client", "连接断开: " << (cause.empty() ? "-" : cause) << "，准备重连");
        cv.notify_all();
        notifyConn(false);
    }
    void delivery_complete(mqtt::delivery_token_ptr) override {
        // 饱和减：避免 waitForPubAck 同步路径（不计数）与历史计数不平衡导致下溢
        std::int64_t v = inflight.load();
        if (v > 0) inflight.fetch_sub(1);
    }
    void message_arrived(mqtt::const_message_ptr msg) override {
        const std::string topic = msg->get_topic();
        const std::string payload = msg->get_payload_str();
        received.fetch_add(1);

        const MessageDesc *desc = findMessage(topic);
        if (desc == nullptr) {
            RM_WARN("client", "收到未知指令: " << topic << " (" << payload.size() << " B)");
            return;
        }
        std::string err;
        auto native = fromWire(*desc, payload.data(), payload.size(), &err);
        if (!native) {
            RM_WARN("client", "解码失败 " << topic << ": " << err);
            return;
        }
        if (opt.logMessages) {
            RM_INFO("client", "↓ " << topic << " (" << payload.size() << " B)");
        }
        dispatcher.dispatch(topic, std::move(native));
    }

    void notifyConn(bool connected) {
        Client::ConnectionHandler cb;
        {
            std::lock_guard<std::mutex> lk(connHandlerMutex);
            cb = connectionHandler;
        }
        if (cb) cb(connected);
    }

    // 订阅：一直重试直到全订阅成功或断线/停止。订阅幂等，重复订阅无害。
    void subscribeAll() {
        const std::vector<std::string> topics =
            opt.subscribeAllDownlink ? downlinkNames() : dispatcher.names();
        if (topics.empty()) return;

        while (running.load() && connectedFlag.load()) {
            std::size_t ok = 0;
            for (const auto &t : topics) {
                if (!running.load() || !connectedFlag.load()) return;
                try {
                    if (client->subscribe(t, 1)->wait_for(std::chrono::seconds(5))) ++ok;
                    else RM_WARN("client", "订阅超时: " << t);
                } catch (const mqtt::exception &e) {
                    RM_WARN("client", "订阅失败 " << t << ": " << e.what());
                }
            }
            if (ok == topics.size()) {
                RM_INFO("client", "订阅完成: " << ok << "/" << topics.size() << " 条下行指令");
                return;
            }
            RM_WARN("client", "订阅不完整 " << ok << "/" << topics.size() << "，稍后重试");
            if (waitRunning(500)) return;
        }
    }

    bool subscribeOne(const std::string &topic) {
        if (!client || !connectedFlag.load()) return false;
        try {
            return client->subscribe(topic, 1)->wait_for(std::chrono::seconds(5));
        } catch (const mqtt::exception &e) {
            RM_ERROR("client", "订阅失败 " << topic << ": " << e.what());
            return false;
        }
    }

    // 尽力排空在途消息，最多 timeoutMs；返回是否排空（inflight==0）
    bool drainInflight(int timeoutMs) {
        const int64_t deadline = nowMs() + timeoutMs;
        while (inflight.load() > 0 && nowMs() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return inflight.load() == 0;
    }
};

Client::Client() : Client(Options{}) {}

Client::Client(Options opt)
    : impl_(std::make_unique<Impl>()) {
    impl_->opt = std::move(opt);
}

Client::~Client() { stop(); }

bool Client::start(std::string *error) {
    (void)error;   // 不再用错误码表达「连不上」—— 连不上就一直后台重试
    const std::string uri = "tcp://" + impl_->opt.host + ":" + std::to_string(impl_->opt.port);
    impl_->client = std::make_unique<mqtt::async_client>(uri, impl_->opt.clientId);
    impl_->client->set_callback(*impl_);

    {
        std::lock_guard<std::mutex> lk(impl_->mutex);
        impl_->running.store(true);
        impl_->connectedFlag.store(false);
        impl_->subscribedOnce.store(false);
    }
    impl_->connectThread = std::thread([this] { impl_->connectLoop(); });

    if (impl_->opt.firstConnectWaitMs > 0) {
        impl_->waitPredicate(
            [this] { return impl_->subscribedOnce.load() || !impl_->running.load(); },
            impl_->opt.firstConnectWaitMs);
    }
    return true;
}

void Client::stop() {
    if (!impl_) return;
    {
        std::lock_guard<std::mutex> lk(impl_->mutex);
        impl_->running.store(false);
    }
    impl_->cv.notify_all();

    if (impl_->client) {
        if (impl_->client->is_connected()) {
            // 已连接：先尽力排空再断开（顺序不能反，否则 pending 消息直接没了）
            impl_->drainInflight(500);
            try {
                impl_->client->disconnect()->wait_for(std::chrono::seconds(3));
            } catch (const mqtt::exception &) {
            }
        } else {
            // 未连接：可能正在 connect 中，用 disconnect 取消在途连接
            try {
                impl_->client->disconnect()->wait_for(std::chrono::seconds(1));
            } catch (const mqtt::exception &) {
            }
        }
    }

    if (impl_->connectThread.joinable()) impl_->connectThread.join();

    // 兜底：迟到的 CONNACK 可能在这之后又让连接成立（connected() 里会因
    // !running 立即断开，但这里再兜一层，保证 stop() 返回时没有活跃连接）。
    if (impl_->client && impl_->client->is_connected()) {
        try {
            impl_->client->disconnect()->wait_for(std::chrono::seconds(3));
        } catch (const mqtt::exception &) {
        }
    }
}

void Client::requestStop() {
    // 信号安全：**绝不能加锁**。信号处理里若取同一把锁、而被打断的线程恰好持锁，
    // 就会永久死锁（已实测复现）。运行标志用原子写；notify 是尽力唤醒，
    // 连接线程的等待都是 wait_for 轮询式，即使这个 notify 丢了也能在几百毫秒内
    // 自己醒来看到 running==false。
    impl_->running.store(false);
    impl_->cv.notify_all();
}

bool Client::isConnected() const { return impl_->connectedFlag.load(); }

void Client::onConnectionChanged(ConnectionHandler cb) {
    std::lock_guard<std::mutex> lk(impl_->connHandlerMutex);
    impl_->connectionHandler = std::move(cb);
}

bool Client::send(std::unique_ptr<Message> msg) {
    if (!msg) {
        RM_ERROR("client", "send: 空指针");
        return false;
    }
    const std::string name = msg->name();
    return send(name, std::move(msg));
}

bool Client::send(const std::string &name, std::unique_ptr<Message> msg) {
    if (!msg) {
        RM_ERROR("client", "send(" << name << "): 空指针");
        return false;
    }
    if (name != msg->name()) {
        RM_ERROR("client", "send: 指令名 '" << name << "' 与结构体类型 '" << msg->name()
                                           << "' 不一致");
        return false;
    }
    const MessageDesc *desc = findMessage(name);
    if (desc == nullptr) {
        RM_ERROR("client", "send: 未知指令 " << name);
        return false;
    }
    if (!impl_->connectedFlag.load()) {
        RM_WARN("client", "send: 未连接，丢弃 " << name << "（后台仍在重连）");
        return false;
    }
    std::string verr;
    if (!validateMessage(*msg, &verr)) {
        RM_ERROR("client", "send: 指令不符合官方协议约束 " << name << ": " << verr);
        return false;
    }
    std::string payload;
    if (!desc->encode(*msg, &payload)) {
        RM_ERROR("client", "send: 序列化失败 " << name);
        return false;
    }

    // 同步/异步两种路径统一走 inflight 计数，delivery_complete 里饱和减，
    // 这样「waitForPubAck=true」也不会让计数下溢。
    impl_->inflight.fetch_add(1);
    try {
        if (impl_->opt.waitForPubAck) {
            impl_->client->publish(name, payload.data(), payload.size(), 1, false)
                ->wait_for(std::chrono::seconds(5));
        } else {
            impl_->client->publish(name, payload.data(), payload.size(), 1, false);
        }
    } catch (const mqtt::exception &e) {
        impl_->inflight.fetch_sub(1);
        RM_ERROR("client", "send: 发布失败 " << name << ": " << e.what());
        return false;
    }
    impl_->sent.fetch_add(1);
    if (impl_->opt.logMessages) {
        RM_INFO("client", "↑ " << name << " (" << payload.size() << " B) => " << toJson(*msg));
    }
    return true;
}

void Client::on(const std::string &name, Handler cb) {
    if (!isKnownMessage(name)) {
        RM_ERROR("client", "on: 未知指令 " << name);
        return;
    }
    const bool already = impl_->dispatcher.has(name);
    impl_->dispatcher.setHandler(name, std::move(cb));
    if (!already && !impl_->opt.subscribeAllDownlink && impl_->running.load()) {
        impl_->subscribeOne(name);
    }
}

void Client::onAny(Handler cb) { impl_->dispatcher.setAny(std::move(cb)); }

void Client::waitFor(std::chrono::milliseconds d) {
    impl_->waitPredicate([this] { return !impl_->running.load(); },
                         static_cast<int64_t>(d.count()));
}

void Client::runUntilStopped() {
    while (impl_->running.load()) {
        impl_->waitPredicate([this] { return !impl_->running.load(); }, 500);
    }
}

std::size_t Client::receivedCount() const { return impl_->received.load(); }
std::size_t Client::sentCount() const { return impl_->sent.load(); }

} // namespace rm2027
