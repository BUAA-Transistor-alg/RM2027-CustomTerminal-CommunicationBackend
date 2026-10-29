#include "rm2027/broker/mqtt_broker.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "rm2027/common/log.h"

namespace rm2027 {

namespace {

constexpr int kTypeConnect = 1;
constexpr int kTypeConnack = 2;
constexpr int kTypePublish = 3;
constexpr int kTypePuback = 4;
constexpr int kTypePubrec = 5;
constexpr int kTypePubrel = 6;
constexpr int kTypePubcomp = 7;
constexpr int kTypeSubscribe = 8;
constexpr int kTypeSuback = 9;
constexpr int kTypeUnsubscribe = 10;
constexpr int kTypeUnsuback = 11;
constexpr int kTypePingreq = 12;
constexpr int kTypePingresp = 13;
constexpr int kTypeDisconnect = 14;

void appendU16(std::string &s, uint16_t v) {
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
    s.push_back(static_cast<char>(v & 0xFF));
}

bool readU16(const std::string &s, std::size_t &pos, uint16_t &v) {
    if (pos + 2 > s.size()) return false;
    v = static_cast<uint16_t>((static_cast<unsigned char>(s[pos]) << 8) |
                              static_cast<unsigned char>(s[pos + 1]));
    pos += 2;
    return true;
}

void appendRemainingLength(std::string &s, std::size_t len) {
    do {
        unsigned char b = len % 128;
        len /= 128;
        if (len > 0) b |= 0x80;
        s.push_back(static_cast<char>(b));
    } while (len > 0);
}

} // namespace

namespace {
std::vector<std::string> splitLevels(const std::string &s) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t p = s.find('/', start);
        if (p == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}
} // namespace

bool topicMatches(const std::string &filter, const std::string &topic) {
    const std::vector<std::string> f = splitLevels(filter);
    const std::vector<std::string> t = splitLevels(topic);
    std::size_t i = 0;
    for (; i < f.size(); ++i) {
        if (f[i] == "#") return true; // 多层通配：匹配剩余全部层级（含零层，故 "a/#" 匹配 "a"）
        if (i >= t.size()) return false;
        if (f[i] != "+" && f[i] != t[i]) return false;
    }
    return i == t.size();
}

// ---------------------------------------------------------------------------

struct Session {
    int fd{-1};
    std::string clientId;
    uint16_t keepaliveSec{0};
    std::vector<std::pair<std::string, int>> subs; // filter -> qos
    std::mutex writeMutex;
    std::atomic<long long> lastActivityMs{0};
    std::atomic<bool> closed{false};
};

struct MqttBroker::Impl {
    int listenFd{-1};
    std::atomic<bool> running{false};
    std::thread acceptThread;
    std::thread reaperThread;

    std::mutex mutex; // 保护 sessions / retained
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions;
    std::unordered_map<std::string, MqttMessage> retained;

    MqttBroker *owner{nullptr};

    static long long nowMs() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    // ---- 底层 IO ----
    static bool writeAll(int fd, const char *data, std::size_t len) {
        std::size_t off = 0;
        while (off < len) {
            const ssize_t n = ::send(fd, data + off, len - off, MSG_NOSIGNAL);
            if (n > 0) {
                off += static_cast<std::size_t>(n);
                continue;
            }
            if (n < 0 && (errno == EINTR)) continue;
            return false;
        }
        return true;
    }

    static bool readExact(int fd, char *buf, std::size_t len) {
        std::size_t off = 0;
        while (off < len) {
            const ssize_t n = ::recv(fd, buf + off, len - off, 0);
            if (n > 0) {
                off += static_cast<std::size_t>(n);
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        return true;
    }

    void sendPacket(const std::shared_ptr<Session> &s, const std::string &packet) {
        if (s->closed.load()) return;
        std::lock_guard<std::mutex> lk(s->writeMutex);
        if (!writeAll(s->fd, packet.data(), packet.size())) {
            s->closed.store(true);
            // 关键：shutdown 会唤醒正阻塞在 recv 上的 serveClient 线程，
            // 让它看到 EOF、退出并 removeSession()。否则只标 closed，
            // 那个线程还卡在 recv 里，会话永远留在 map 里（reaper 也不清它）。
            ::shutdown(s->fd, SHUT_RDWR);
        }
    }

    static std::string fixedHeader(int type, int flags, std::size_t remaining) {
        std::string h;
        h.push_back(static_cast<char>((type << 4) | (flags & 0x0F)));
        appendRemainingLength(h, remaining);
        return h;
    }

    // ---- 会话管理 ----
    void removeSession(const std::string &clientId, int fd) {
        bool removed = false;
        {
            std::lock_guard<std::mutex> lk(mutex);
            auto it = sessions.find(clientId);
            if (it != sessions.end() && it->second->fd == fd) {
                sessions.erase(it);
                removed = true;
            }
        }
        if (removed && owner->onDisconnect_) owner->onDisconnect_(clientId);
    }

    void deliver(const MqttMessage &msg, const std::string &fromClientId) {
        std::vector<std::shared_ptr<Session>> targets;
        {
            std::lock_guard<std::mutex> lk(mutex);
            for (auto &kv : sessions) {
                const auto &s = kv.second;
                if (!fromClientId.empty() && s->clientId == fromClientId) {
                    // MQTT 不把消息回发给发布者自己（除非它也订阅了；此处保留标准行为：仍然投递）
                }
                for (const auto &sub : s->subs) {
                    if (topicMatches(sub.first, msg.topic)) {
                        targets.push_back(s);
                        break;
                    }
                }
            }
        }
        for (auto &s : targets) {
            int qos = msg.qos;
            for (const auto &sub : s->subs) {
                if (topicMatches(sub.first, msg.topic)) {
                    qos = std::min(qos, sub.second);
                    break;
                }
            }
            std::string pkt;
            std::string body;
            appendU16(body, static_cast<uint16_t>(msg.topic.size()));
            body += msg.topic;
            if (qos > 0) {
                static std::atomic<uint16_t> pid{1};
                uint16_t id = pid.fetch_add(1);
                if (id == 0) id = pid.fetch_add(1);
                appendU16(body, id);
            }
            body += msg.payload;
            // 重新拼固定头（长度已知）
            std::string full = fixedHeader(kTypePublish, (msg.retain ? 1 : 0) | ((qos & 0x3) << 1),
                                           body.size());
            full += body;
            sendPacket(s, full);
        }
    }

    void handlePublish(const std::shared_ptr<Session> &s, int flags, const std::string &body) {
        const int qos = (flags >> 1) & 0x03;
        const bool retain = (flags & 0x01) != 0;
        std::size_t pos = 0;
        uint16_t topicLen = 0;
        if (!readU16(body, pos, topicLen) || pos + topicLen > body.size()) return;
        MqttMessage msg;
        msg.topic = body.substr(pos, topicLen);
        pos += topicLen;
        uint16_t packetId = 0;
        if (qos > 0) {
            if (!readU16(body, pos, packetId)) return;
        }
        msg.payload = body.substr(pos);
        msg.qos = qos;
        msg.retain = retain;

        if (qos == 1) {
            std::string ack = fixedHeader(kTypePuback, 0, 2);
            appendU16(ack, packetId);
            sendPacket(s, ack);
        } else if (qos == 2) {
            std::string rec = fixedHeader(kTypePubrec, 0, 2);
            appendU16(rec, packetId);
            sendPacket(s, rec);
            // 简化：QoS2 也在此刻投递（测试场景足够，且不会重复）
        }

        if (retain) {
            std::lock_guard<std::mutex> lk(mutex);
            if (msg.payload.empty()) {
                retained.erase(msg.topic);
            } else {
                retained[msg.topic] = msg;
            }
        }

        RM_DEBUG("broker", "PUBLISH qos=" << qos << " topic=" << msg.topic << " bytes="
                                          << msg.payload.size() << " from=" << s->clientId);
        if (owner->onPublish_) owner->onPublish_(s->clientId, msg);
        deliver(msg, s->clientId);
    }

    void handleSubscribe(const std::shared_ptr<Session> &s, const std::string &body) {
        std::size_t pos = 0;
        uint16_t packetId = 0;
        if (!readU16(body, pos, packetId)) return;
        std::vector<int> granted;
        std::vector<std::pair<std::string, int>> added;
        while (pos < body.size()) {
            uint16_t len = 0;
            if (!readU16(body, pos, len) || pos + len > body.size()) return;
            std::string filter = body.substr(pos, len);
            pos += len;
            if (pos >= body.size()) return;
            int qos = static_cast<unsigned char>(body[pos]);
            pos += 1;
            if (qos > 2) qos = 1; // 只支持到 QoS 1，超过则降级
            if (qos > 1) qos = 1;
            added.emplace_back(filter, qos);
            granted.push_back(qos);
            RM_INFO("broker", "SUBSCRIBE " << s->clientId << " -> " << filter << " (qos " << qos << ")");
        }
        {
            std::lock_guard<std::mutex> lk(mutex);
            for (auto &a : added) {
                bool found = false;
                for (auto &existing : s->subs) {
                    if (existing.first == a.first) {
                        existing.second = a.second;
                        found = true;
                        break;
                    }
                }
                if (!found) s->subs.push_back(a);
            }
        }
        std::string ack = fixedHeader(kTypeSuback, 0, 2 + granted.size());
        appendU16(ack, packetId);
        for (int g : granted) ack.push_back(static_cast<char>(g));
        sendPacket(s, ack);

        // 下发 retain 消息
        std::vector<MqttMessage> rets;
        {
            std::lock_guard<std::mutex> lk(mutex);
            for (auto &kv : retained) {
                for (auto &a : added) {
                    if (topicMatches(a.first, kv.first)) {
                        rets.push_back(kv.second);
                        break;
                    }
                }
            }
        }
        for (auto &m : rets) {
            MqttMessage r = m;
            r.retain = true;
            std::string body2;
            appendU16(body2, static_cast<uint16_t>(r.topic.size()));
            body2 += r.topic;
            body2 += r.payload;
            std::string full = fixedHeader(kTypePublish, 0x01, body2.size());
            full += body2;
            sendPacket(s, full);
        }
    }

    void handleUnsubscribe(const std::shared_ptr<Session> &s, const std::string &body) {
        std::size_t pos = 0;
        uint16_t packetId = 0;
        if (!readU16(body, pos, packetId)) return;
        std::lock_guard<std::mutex> lk(mutex);
        while (pos < body.size()) {
            uint16_t len = 0;
            if (!readU16(body, pos, len) || pos + len > body.size()) break;
            std::string filter = body.substr(pos, len);
            pos += len;
            for (auto it = s->subs.begin(); it != s->subs.end();) {
                if (it->first == filter)
                    it = s->subs.erase(it);
                else
                    ++it;
            }
        }
        std::string ack = fixedHeader(kTypeUnsuback, 0, 2);
        appendU16(ack, packetId);
        sendPacket(s, ack);
    }

    bool handleConnect(const std::shared_ptr<Session> &s, const std::string &body,
                       std::string &assignedId) {
        std::size_t pos = 0;
        uint16_t nameLen = 0;
        if (!readU16(body, pos, nameLen) || pos + nameLen > body.size()) return false;
        const std::string protoName = body.substr(pos, nameLen);
        pos += nameLen;
        if (pos + 4 > body.size()) return false;
        const int level = static_cast<unsigned char>(body[pos]);
        pos += 1;
        const unsigned char connectFlags = static_cast<unsigned char>(body[pos]);
        pos += 1;
        uint16_t keepalive = 0;
        if (!readU16(body, pos, keepalive)) return false;
        (void)connectFlags;

        if (protoName != "MQTT" || level != 4) {
            RM_WARN("broker", "不支持的协议: " << protoName << " level=" << level);
            std::string nack = fixedHeader(kTypeConnack, 0, 2);
            nack.push_back(0x00);
            nack.push_back(0x01); // 不支持的协议版本
            sendPacket(s, nack);
            return false;
        }

        uint16_t idLen = 0;
        if (!readU16(body, pos, idLen) || pos + idLen > body.size()) return false;
        std::string clientId = body.substr(pos, idLen);
        if (clientId.empty()) clientId = "anon-" + std::to_string(s->fd);

        // will / username / password 解析（本 broker 不使用 will，仅跳过）
        // 注意：这里不校验剩余字段，MQTT 允许 payload 里带 will/user/pass

        assignedId = clientId;
        s->clientId = clientId;
        s->keepaliveSec = keepalive;
        s->lastActivityMs.store(nowMs());

        // 同 ID 顶号：断开旧连接
        {
            std::shared_ptr<Session> old;
            {
                std::lock_guard<std::mutex> lk(mutex);
                auto it = sessions.find(clientId);
                if (it != sessions.end()) old = it->second;
            }
            if (old && old->fd != s->fd) {
                RM_WARN("broker", "客户端 ID 重复，断开旧连接: " << clientId);
                old->closed.store(true);
                ::shutdown(old->fd, SHUT_RDWR);
            }
        }
        {
            std::lock_guard<std::mutex> lk(mutex);
            sessions[clientId] = s;
        }

        std::string connack = fixedHeader(kTypeConnack, 0, 2);
        connack.push_back(0x00); // session present
        connack.push_back(0x00); // accepted
        sendPacket(s, connack);
        RM_INFO("broker", "CONNECT 接受: clientId=" << clientId << " keepalive=" << keepalive << "s");
        if (owner->onConnect_) owner->onConnect_(clientId);
        return true;
    }

    void serveClient(int fd, const std::string &peer) {
        auto s = std::make_shared<Session>();
        s->fd = fd;
        s->lastActivityMs.store(nowMs());
        std::string clientId = peer;
        bool connected = false;

        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        while (running.load() && !s->closed.load()) {
            unsigned char hdr = 0;
            if (!readExact(fd, reinterpret_cast<char *>(&hdr), 1)) break;
            s->lastActivityMs.store(nowMs());
            const int type = (hdr >> 4) & 0x0F;
            const int flags = hdr & 0x0F;

            std::size_t remaining = 0;
            int multiplier = 1;
            unsigned char byte = 0;
            do {
                if (!readExact(fd, reinterpret_cast<char *>(&byte), 1)) {
                    s->closed.store(true);
                    break;
                }
                remaining += static_cast<std::size_t>(byte & 0x7F) * multiplier;
                multiplier *= 128;
                if (multiplier > 128 * 128 * 128 * 128) {
                    s->closed.store(true);
                    break;
                }
            } while ((byte & 0x80) != 0);
            if (s->closed.load()) break;
            if (remaining > 8 * 1024 * 1024) break; // 防呆

            std::string body;
            body.resize(remaining);
            if (remaining > 0 && !readExact(fd, &body[0], remaining)) break;
            s->lastActivityMs.store(nowMs());

            switch (type) {
            case kTypeConnect: {
                std::string id;
                if (!handleConnect(s, body, id)) {
                    s->closed.store(true);
                } else {
                    clientId = id;
                    connected = true;
                }
                break;
            }
            case kTypePublish:
                if (!connected) break;
                handlePublish(s, flags, body);
                break;
            case kTypePuback:
            case kTypePubrec:
            case kTypePubcomp:
                break; // 单机测试 broker 不做重传
            case kTypePubrel: {
                if (body.size() >= 2) {
                    std::string comp = fixedHeader(kTypePubcomp, 0, 2);
                    comp += body.substr(0, 2);
                    sendPacket(s, comp);
                }
                break;
            }
            case kTypeSubscribe:
                if (!connected) break;
                handleSubscribe(s, body);
                break;
            case kTypeUnsubscribe:
                if (!connected) break;
                handleUnsubscribe(s, body);
                break;
            case kTypePingreq: {
                std::string resp = fixedHeader(kTypePingresp, 0, 0);
                sendPacket(s, resp);
                break;
            }
            case kTypeDisconnect:
                s->closed.store(true);
                break;
            default:
                RM_WARN("broker", "未知报文类型: " << type);
                break;
            }
        }

        s->closed.store(true);
        if (connected) removeSession(clientId, fd);
        ::close(fd);
        RM_INFO("broker", "连接关闭: " << clientId);
    }

    void acceptLoop() {
        while (running.load()) {
            sockaddr_in addr{};
            socklen_t len = sizeof(addr);
            const int fd = ::accept(listenFd, reinterpret_cast<sockaddr *>(&addr), &len);
            if (fd < 0) {
                if (errno == EINTR) continue;
                if (!running.load()) break;
                RM_WARN("broker", "accept 失败: " << std::strerror(errno));
                continue;
            }
            // 发送超时（关键）：向一个半死/停止读取的连接 send() 会无限阻塞，
            // 而 deliver() 是同步逐个会话发送的 —— 一个死连接会把投递给
            // **所有**客户端的线程一起卡死。设 2 秒超时，超时后 writeAll
            // 失败、会话被标记 closed 并由 reaper 清理，把伤害限制在 2 秒内。
            struct timeval sndto{2, 0};
            ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &sndto, sizeof(sndto));
            char ip[64] = {0};
            ::inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
            const std::string peer = std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
            RM_DEBUG("broker", "新 TCP 连接: " << peer);
            std::thread(&Impl::serveClient, this, fd, peer).detach();
        }
    }

    void reaperLoop() {
        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            const long long now = nowMs();
            std::vector<std::shared_ptr<Session>> dead;
            {
                std::lock_guard<std::mutex> lk(mutex);
                for (auto &kv : sessions) {
                    if (kv.second->keepaliveSec == 0) continue;
                    const long long limit = static_cast<long long>(kv.second->keepaliveSec) * 1500;
                    if (now - kv.second->lastActivityMs.load() > limit) dead.push_back(kv.second);
                }
            }
            for (auto &s : dead) {
                RM_WARN("broker", "keepalive 超时，断开: " << s->clientId);
                s->closed.store(true);
                ::shutdown(s->fd, SHUT_RDWR);
            }
        }
    }
};

// ---------------------------------------------------------------------------

MqttBroker::MqttBroker() : impl_(std::make_unique<Impl>()) { impl_->owner = this; }
MqttBroker::~MqttBroker() { stop(); }

bool MqttBroker::listen(const std::string &host, uint16_t port, std::string *error) {
    impl_->listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (impl_->listenFd < 0) {
        if (error) *error = std::string("socket() 失败: ") + std::strerror(errno);
        return false;
    }
    int one = 1;
    ::setsockopt(impl_->listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (host.empty() || host == "0.0.0.0" || host == "*") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        if (error) *error = "无法解析监听地址: " + host;
        ::close(impl_->listenFd);
        impl_->listenFd = -1;
        return false;
    }

    if (::bind(impl_->listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        if (error) *error = "bind(" + host + ":" + std::to_string(port) +
                            ") 失败: " + std::strerror(errno);
        ::close(impl_->listenFd);
        impl_->listenFd = -1;
        return false;
    }
    if (::listen(impl_->listenFd, 64) < 0) {
        if (error) *error = std::string("listen() 失败: ") + std::strerror(errno);
        ::close(impl_->listenFd);
        impl_->listenFd = -1;
        return false;
    }

    impl_->running.store(true);
    impl_->acceptThread = std::thread(&Impl::acceptLoop, impl_.get());
    impl_->reaperThread = std::thread(&Impl::reaperLoop, impl_.get());
    return true;
}

void MqttBroker::stop() {
    if (!impl_) return;
    if (!impl_->running.exchange(false)) return;

    if (impl_->listenFd >= 0) {
        ::shutdown(impl_->listenFd, SHUT_RDWR);
        ::close(impl_->listenFd);
        impl_->listenFd = -1;
    }
    if (impl_->acceptThread.joinable()) impl_->acceptThread.join();
    if (impl_->reaperThread.joinable()) impl_->reaperThread.join();

    std::vector<std::shared_ptr<Session>> all;
    {
        std::lock_guard<std::mutex> lk(impl_->mutex);
        for (auto &kv : impl_->sessions) all.push_back(kv.second);
        impl_->sessions.clear();
    }
    for (auto &s : all) {
        s->closed.store(true);
        ::shutdown(s->fd, SHUT_RDWR);
    }
}

void MqttBroker::publish(const MqttMessage &msg) { impl_->deliver(msg, std::string()); }

std::size_t MqttBroker::clientCount() const {
    std::lock_guard<std::mutex> lk(impl_->mutex);
    return impl_->sessions.size();
}

std::size_t MqttBroker::subscribedClientCount() const {
    std::lock_guard<std::mutex> lk(impl_->mutex);
    std::size_t n = 0;
    for (const auto &kv : impl_->sessions) {
        if (!kv.second->subs.empty()) ++n;
    }
    return n;
}

std::size_t MqttBroker::topicSubscriberCount(const std::string &topic) const {
    std::lock_guard<std::mutex> lk(impl_->mutex);
    std::size_t n = 0;
    for (const auto &kv : impl_->sessions) {
        for (const auto &sub : kv.second->subs) {
            if (topicMatches(sub.first, topic)) {
                ++n;
                break;
            }
        }
    }
    return n;
}

std::vector<std::string> MqttBroker::clientIds() const {
    std::lock_guard<std::mutex> lk(impl_->mutex);
    std::vector<std::string> v;
    v.reserve(impl_->sessions.size());
    for (auto &kv : impl_->sessions) v.push_back(kv.first);
    return v;
}

} // namespace rm2027
