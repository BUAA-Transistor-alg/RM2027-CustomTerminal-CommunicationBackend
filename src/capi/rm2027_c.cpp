// RM2027 纯 C 接口的实现 —— 薄薄一层，转调 C++ 门面（rm2027::Client / rm2027::Server）
#include "rm2027/capi/rm2027_c.h"

#include <cctype>
#include <cstring>
#include <map>
#include <new>
#include <memory>
#include <string>

#include "rm2027/api/client.h"
#include "rm2027/api/detail/registry.h"
#include "rm2027/api/message.h"
#include "rm2027/api/server.h"
#include "rm2027/api/validate.h"
#include "rm2027/api/wire_format.h"
#include "rm2027/video/video_receiver.h"
#include "rm2027/video/video_sender.h"

using namespace rm2027;

namespace {

// 每个 handle 的实际内容
// ---------------------------------------------------------------- 极简扁平 JSON
// 只支持 {"key": 数字|"字符串"|true|false} 这种一层对象 —— 配置参数够用了。
bool jsonFlat(const char *text, std::map<std::string, std::string> *out) {
    if (!text || !out) return false;
    const std::string s(text);
    std::size_t i = 0;
    auto skipWs = [&] { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; };
    skipWs();
    if (i >= s.size() || s[i] != '{') return false;
    ++i;
    while (true) {
        skipWs();
        if (i < s.size() && s[i] == '}') return true;
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        std::string key;
        while (i < s.size() && s[i] != '"') key += s[i++];
        if (i >= s.size()) return false;
        ++i;
        skipWs();
        if (i >= s.size() || s[i] != ':') return false;
        ++i;
        skipWs();
        std::string val;
        if (i < s.size() && s[i] == '"') {
            ++i;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size()) ++i;
                val += s[i++];
            }
            if (i >= s.size()) return false;
            ++i;
        } else {
            while (i < s.size() && s[i] != ',' && s[i] != '}') val += s[i++];
            while (!val.empty() && std::isspace(static_cast<unsigned char>(val.back()))) val.pop_back();
        }
        (*out)[key] = val;
        skipWs();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') return true;
        return false;
    }
}

int jsonInt(const std::map<std::string, std::string> &m, const char *k, int def) {
    auto it = m.find(k);
    if (it == m.end() || it->second.empty()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
}
double jsonDouble(const std::map<std::string, std::string> &m, const char *k, double def) {
    auto it = m.find(k);
    if (it == m.end() || it->second.empty()) return def;
    try { return std::stod(it->second); } catch (...) { return def; }
}
bool jsonBool(const std::map<std::string, std::string> &m, const char *k, bool def) {
    auto it = m.find(k);
    if (it == m.end()) return def;
    const std::string &v = it->second;
    return v == "true" || v == "1" || v == "yes";
}
std::string jsonStr(const std::map<std::string, std::string> &m, const char *k,
                    const std::string &def) {
    auto it = m.find(k);
    return it == m.end() ? def : it->second;
}
bool jsonHas(const std::map<std::string, std::string> &m, const char *k) {
    return m.find(k) != m.end();
}

// 把统计 JSON 拷进调用方缓冲
int copyOut(const std::string &json, char *buf, int buflen) {
    const int need = static_cast<int>(json.size());
    if (!buf || buflen <= need) return -1;
    std::memcpy(buf, json.c_str(), static_cast<std::size_t>(need) + 1);
    return need;
}

struct RmHandle {
    enum class Kind { Client, Server, VideoSender, VideoReceiver } kind;
    std::unique_ptr<Client> client;
    std::unique_ptr<Server> server;
    std::unique_ptr<VideoSender> videoSender;
    std::unique_ptr<VideoReceiver> videoReceiver;
    // 发送端配置：先攒在 options 里，start 时用
    VideoSenderOptions senderOpt;

    rm_message_cb messageCb = nullptr;
    void *messageUser = nullptr;
    rm_event_cb eventCb = nullptr;
    void *eventUser = nullptr;
};

std::string &lastError() {
    thread_local std::string err;
    return err;
}

int fail(const std::string &msg) {
    lastError() = msg;
    return 1;
}

void emitEvent(RmHandle *h, const char *event, const std::string &detail) {
    if (h && h->eventCb) h->eventCb(h->eventUser, event, detail.c_str());
}

void emitMessage(RmHandle *h, const std::string &name, const std::string &json,
                 const std::string &clientId) {
    if (h && h->messageCb) h->messageCb(h->messageUser, name.c_str(), json.c_str(),
                                        clientId.c_str());
}

const MessageDesc *descAt(int index) {
    const int n = rm_message_count();
    if (index < 0 || index >= n) return nullptr;
    std::size_t count = 0;
    const MessageDesc *table = messageTable(&count);
    return &table[index];
}

const MessageDesc *descByName(const char *name) {
    if (!name) return nullptr;
    return findMessage(name);
}

} // namespace

extern "C" {

// ---------------------------------------------------------------- 元数据

int rm_message_count(void) {
    std::size_t n = 0;
    messageTable(&n);
    return static_cast<int>(n);
}

const char *rm_message_name(int index) {
    const MessageDesc *d = descAt(index);
    return d ? d->name : nullptr;
}

const char *rm_message_direction(int index) {
    const MessageDesc *d = descAt(index);
    return d ? directionName(d->direction) : nullptr;
}

int rm_message_triggered(int index) {
    const MessageDesc *d = descAt(index);
    return d ? d->triggered : 0;
}

double rm_message_default_hz(int index) {
    const MessageDesc *d = descAt(index);
    return d ? d->default_hz : 0.0;
}

double rm_message_max_hz(int index) {
    const MessageDesc *d = descAt(index);
    return d ? d->max_hz : 0.0;
}

const char *rm_message_fields_json(int index) {
    const MessageDesc *d = descAt(index);
    return d ? d->fields_json : nullptr;
}

const char *rm_message_display(int index) {
    const MessageDesc *d = descAt(index);
    return d ? d->name : nullptr;
}

int rm_message_index(const char *name) {
    const MessageDesc *d = descByName(name);
    if (!d) return -1;
    std::size_t count = 0;
    const MessageDesc *table = messageTable(&count);
    return static_cast<int>(d - table);
}

int rm_message_triggered_by_name(const char *name) {
    const MessageDesc *d = descByName(name);
    return d ? d->triggered : 0;
}

double rm_message_default_hz_by_name(const char *name) {
    const MessageDesc *d = descByName(name);
    return d ? d->default_hz : 0.0;
}

double rm_message_max_hz_by_name(const char *name) {
    const MessageDesc *d = descByName(name);
    return d ? d->max_hz : 0.0;
}

const char *rm_message_fields_json_by_name(const char *name) {
    const MessageDesc *d = descByName(name);
    return d ? d->fields_json : nullptr;
}

// ---------------------------------------------------------------- 客户端

rm_handle rm_client_new(const char *host, uint16_t port, const char *client_id) {
    auto *h = new (std::nothrow) RmHandle();
    if (!h) {
        lastError() = "内存不足";
        return nullptr;
    }
    h->kind = RmHandle::Kind::Client;
    Client::Options opt;
    if (host) opt.host = host;
    opt.port = port;
    if (client_id) opt.clientId = client_id;
    opt.logMessages = false; // C 侧由调用者自己决定怎么记录
    h->client = std::make_unique<Client>(opt);
    h->client->onAny([h](std::unique_ptr<Message> msg) {
        const std::string name = msg->name();
        emitMessage(h, name, toJson(*msg), std::string());
    });
    // 连接状态变化也作为事件抛给上层（"connected" / "disconnected"），
    // 这样 GUI 能即时显示「连接中/重连中」，而不是等 500ms 轮询才发现。
    h->client->onConnectionChanged([h](bool connected) {
        emitEvent(h, connected ? "connected" : "disconnected", std::string());
    });
    return h;
}

void rm_client_free(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    if (!h) return;
    if (h->client) h->client->stop();
    delete h;
}

int rm_client_start(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    if (!h || !h->client) return fail("无效句柄");
    std::string err;
    if (!h->client->start(&err)) return fail(err);
    return 0;
}

void rm_client_stop(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    if (h && h->client) h->client->stop();
}

int rm_client_is_connected(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    return (h && h->client && h->client->isConnected()) ? 1 : 0;
}

int rm_client_send(rm_handle handle, const char *name, const char *json) {
    auto *h = static_cast<RmHandle *>(handle);
    if (!h || !h->client) return fail("无效句柄");
    if (!name || !json) return fail("name/json 为空");
    std::string err;
    auto msg = fromJson(name, json, &err);
    if (!msg) return fail("构造指令失败: " + err);
    std::string verr;
    if (!validateMessage(*msg, &verr)) return fail("指令不符合协议约束: " + verr);
    if (!h->client->send(std::move(msg))) return fail("发送失败: " + std::string(name));
    return 0;
}

size_t rm_client_received(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    return (h && h->client) ? h->client->receivedCount() : 0;
}

size_t rm_client_sent(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    return (h && h->client) ? h->client->sentCount() : 0;
}

// ---------------------------------------------------------------- 服务器

rm_handle rm_server_new(const char *host, uint16_t port) {
    auto *h = new (std::nothrow) RmHandle();
    if (!h) {
        lastError() = "内存不足";
        return nullptr;
    }
    h->kind = RmHandle::Kind::Server;
    Server::Options opt;
    if (host) opt.host = host;
    opt.port = port;
    opt.logMessages = false;
    h->server = std::make_unique<Server>(opt);
    h->server->onAnyFrom([h](std::unique_ptr<Message> msg, const std::string &clientId) {
        const std::string name = msg->name();
        emitMessage(h, name, toJson(*msg), clientId);
    });
    h->server->onClientConnected([h](const std::string &id) {
        emitEvent(h, "client-connected", id);
    });
    h->server->onClientDisconnected([h](const std::string &id) {
        emitEvent(h, "client-disconnected", id);
    });
    return h;
}

void rm_server_free(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    if (!h) return;
    if (h->server) h->server->stop();
    delete h;
}

int rm_server_start(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    if (!h || !h->server) return fail("无效句柄");
    std::string err;
    if (!h->server->start(&err)) return fail(err);
    return 0;
}

void rm_server_stop(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    if (h && h->server) h->server->stop();
}

int rm_server_is_running(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    return (h && h->server && h->server->isRunning()) ? 1 : 0;
}

int rm_server_send(rm_handle handle, const char *name, const char *json) {
    auto *h = static_cast<RmHandle *>(handle);
    if (!h || !h->server) return fail("无效句柄");
    if (!name || !json) return fail("name/json 为空");
    std::string err;
    auto msg = fromJson(name, json, &err);
    if (!msg) return fail("构造指令失败: " + err);
    std::string verr;
    if (!validateMessage(*msg, &verr)) return fail("指令不符合协议约束: " + verr);
    if (!h->server->send(std::move(msg))) return fail("发送失败: " + std::string(name));
    return 0;
}

size_t rm_server_client_count(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    return (h && h->server) ? h->server->clientCount() : 0;
}

size_t rm_server_subscribed_client_count(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    return (h && h->server) ? h->server->subscribedClientCount() : 0;
}

size_t rm_server_topic_subscriber_count(rm_handle handle, const char *topic) {
    auto *h = static_cast<RmHandle *>(handle);
    if (!h || !h->server || !topic) return 0;
    return h->server->topicSubscriberCount(topic);
}

size_t rm_server_received(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    return (h && h->server) ? h->server->receivedCount() : 0;
}

size_t rm_server_sent(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    return (h && h->server) ? h->server->sentCount() : 0;
}

// ---------------------------------------------------------------- 回调

void rm_set_message_cb(rm_handle handle, rm_message_cb cb, void *user) {
    auto *h = static_cast<RmHandle *>(handle);
    if (!h) return;
    h->messageCb = cb;
    h->messageUser = user;
}

void rm_set_event_cb(rm_handle handle, rm_event_cb cb, void *user) {
    auto *h = static_cast<RmHandle *>(handle);
    if (!h) return;
    h->eventCb = cb;
    h->eventUser = user;
}

// ---------------------------------------------------------------- 工具

const char *rm_last_error(void) { return lastError().c_str(); }

const char *rm_version(void) { return "rm2027-capi/1.2 (protocol V1.0.0 20261029)"; }

// ---------------------------------------------------------------- 雷达线格式

int rm_set_radar_wire_format(int format) {
    if (format != 0 && format != 1) return fail("雷达线格式只能是 0(nested) 或 1(flat)");
    setRadarWireFormat(format == 0 ? RadarWireFormat::NestedMessage
                                   : RadarWireFormat::FlatPackedUint32);
    return 0;
}

int rm_get_radar_wire_format(void) { return static_cast<int>(radarWireFormat()); }

// ================================================================ 图传

namespace {
RmHandle *asSender(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    if (!h || h->kind != RmHandle::Kind::VideoSender || !h->videoSender) {
        lastError() = "无效的图传发送端句柄";
        return nullptr;
    }
    return h;
}
RmHandle *asReceiver(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    if (!h || h->kind != RmHandle::Kind::VideoReceiver || !h->videoReceiver) {
        lastError() = "无效的图传接收端句柄";
        return nullptr;
    }
    return h;
}
} // namespace

rm_handle rm_video_sender_new(const char *host, uint16_t port) {
    auto *h = new (std::nothrow) RmHandle();
    if (!h) return nullptr;
    h->kind = RmHandle::Kind::VideoSender;
    h->senderOpt.targetHost = (host && *host) ? host : "127.0.0.1";
    h->senderOpt.targetPort = port;
    h->videoSender = std::make_unique<VideoSender>(h->senderOpt);
    return h;
}

void rm_video_sender_free(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    if (!h) return;
    if (h->videoSender) h->videoSender->stop();
    delete h;
}

int rm_video_sender_start(rm_handle handle) {
    auto *h = asSender(handle);
    if (!h) return 1;
    std::string err;
    if (!h->videoSender->start(&err)) return fail(err);
    return 0;
}

void rm_video_sender_stop(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    if (h && h->videoSender) h->videoSender->stop();
}

int rm_video_sender_is_running(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    return (h && h->videoSender && h->videoSender->running()) ? 1 : 0;
}

int rm_video_sender_config(rm_handle handle, const char *json) {
    auto *h = asSender(handle);
    if (!h) return 1;
    if (h->videoSender->running()) return fail("发送端运行中，请先 stop 再改编码参数");
    std::map<std::string, std::string> m;
    if (!jsonFlat(json, &m)) return fail("编码参数不是合法的扁平 JSON 对象");

    EncoderConfig &c = h->senderOpt.encoder;
    if (jsonHas(m, "width")) c.width = jsonInt(m, "width", c.width);
    if (jsonHas(m, "height")) c.height = jsonInt(m, "height", c.height);
    if (jsonHas(m, "fps")) c.fps = jsonInt(m, "fps", c.fps);
    if (jsonHas(m, "bitrate_kbps")) c.bitrateKbps = jsonInt(m, "bitrate_kbps", c.bitrateKbps);
    if (jsonHas(m, "gop_size")) c.gopSize = jsonInt(m, "gop_size", c.gopSize);
    if (jsonHas(m, "bframes")) c.bFrames = jsonInt(m, "bframes", c.bFrames);
    if (jsonHas(m, "slices")) c.slices = jsonInt(m, "slices", c.slices);
    if (jsonHas(m, "aud")) c.aud = jsonBool(m, "aud", c.aud);
    if (jsonHas(m, "repeat_headers")) c.repeatHeaders = jsonBool(m, "repeat_headers", c.repeatHeaders);
    if (jsonHas(m, "intra_refresh")) c.intraRefresh = jsonBool(m, "intra_refresh", c.intraRefresh);
    if (jsonHas(m, "pix_fmt")) c.pixFmt = jsonStr(m, "pix_fmt", c.pixFmt);
    if (jsonHas(m, "preset")) c.preset = jsonStr(m, "preset", c.preset);
    if (jsonHas(m, "tune")) c.tune = jsonStr(m, "tune", c.tune);
    if (jsonHas(m, "x265_lookahead")) c.x265Lookahead = jsonInt(m, "x265_lookahead", c.x265Lookahead);
    if (jsonHas(m, "extra_x265_params"))
        c.extraX265Params = jsonStr(m, "extra_x265_params", c.extraX265Params);
    h->videoSender->setOptions(h->senderOpt);
    return 0;
}

int rm_video_sender_fault(rm_handle handle, const char *json) {
    auto *h = asSender(handle);
    if (!h) return 1;
    if (h->videoSender->running()) return fail("发送端运行中，请先 stop 再改故障注入参数");
    std::map<std::string, std::string> m;
    if (!jsonFlat(json, &m)) return fail("故障参数不是合法的扁平 JSON 对象");
    VideoSenderOptions &o = h->senderOpt;
    if (jsonHas(m, "target_host")) o.targetHost = jsonStr(m, "target_host", o.targetHost);
    if (jsonHas(m, "target_port"))
        o.targetPort = static_cast<uint16_t>(jsonInt(m, "target_port", o.targetPort));
    if (jsonHas(m, "max_datagram"))
        o.maxDatagram = static_cast<std::size_t>(jsonInt(m, "max_datagram", 1400));
    if (jsonHas(m, "drop_every_packet"))
        o.dropEveryNthPacket = jsonInt(m, "drop_every_packet", 0);
    if (jsonHas(m, "duplicate_every_packet"))
        o.duplicateEveryNthPacket = jsonInt(m, "duplicate_every_packet", 0);
    if (jsonHas(m, "shuffle_fragments"))
        o.shuffleFragments = jsonBool(m, "shuffle_fragments", false);
    if (jsonHas(m, "corrupt_frame_total"))
        o.corruptFrameTotal = jsonBool(m, "corrupt_frame_total", false);
    if (jsonHas(m, "frame_number_step")) o.frameNumberStep = jsonInt(m, "frame_number_step", 1);
    if (jsonHas(m, "rotate_every_frames"))
        o.rotateEncoderEveryNFrames = jsonInt(m, "rotate_every_frames", 0);
    h->videoSender->setOptions(o);
    return 0;
}

int rm_video_sender_send_bgr(rm_handle handle, const uint8_t *bgr, int width, int height,
                             int stride) {
    auto *h = asSender(handle);
    if (!h) return 1;
    if (!bgr || width <= 0 || height <= 0) return fail("无效的图像参数");
    if (stride <= 0) stride = width * 3;
    std::string err;
    if (!h->videoSender->sendFrame(bgr, width, height, stride, &err)) return fail(err);
    return 0;
}

int rm_video_sender_flush(rm_handle handle) {
    auto *h = asSender(handle);
    if (!h) return 1;
    std::string err;
    if (!h->videoSender->flush(&err)) return fail(err);
    return 0;
}

int rm_video_sender_stats(rm_handle handle, char *buf, int buflen) {
    auto *h = asSender(handle);
    if (!h) return -1;
    return copyOut(videoStatsToJson(h->videoSender->stats()), buf, buflen);
}

rm_handle rm_video_receiver_new(const char *bindHost, uint16_t port) {
    auto *h = new (std::nothrow) RmHandle();
    if (!h) return nullptr;
    h->kind = RmHandle::Kind::VideoReceiver;
    VideoReceiverOptions o;
    o.bindHost = (bindHost && *bindHost) ? bindHost : "0.0.0.0";
    o.port = port;
    h->videoReceiver = std::make_unique<VideoReceiver>(o);
    return h;
}

void rm_video_receiver_free(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    if (!h) return;
    if (h->videoReceiver) h->videoReceiver->stop();
    delete h;
}

int rm_video_receiver_start(rm_handle handle) {
    auto *h = asReceiver(handle);
    if (!h) return 1;
    std::string err;
    if (!h->videoReceiver->start(&err)) return fail(err);
    return 0;
}

void rm_video_receiver_stop(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    if (h && h->videoReceiver) h->videoReceiver->stop();
}

int rm_video_receiver_is_running(rm_handle handle) {
    auto *h = static_cast<RmHandle *>(handle);
    return (h && h->videoReceiver && h->videoReceiver->running()) ? 1 : 0;
}

int rm_video_receiver_config(rm_handle handle, const char *json) {
    auto *h = asReceiver(handle);
    if (!h) return 1;
    if (h->videoReceiver->running()) return fail("接收端运行中，请先 stop 再改参数");
    std::map<std::string, std::string> m;
    if (!jsonFlat(json, &m)) return fail("接收参数不是合法的扁平 JSON 对象");
    VideoReceiverOptions o = h->videoReceiver->options();
    if (jsonHas(m, "max_frame_bytes"))
        o.maxFrameBytes = static_cast<std::size_t>(jsonInt(m, "max_frame_bytes", 8 << 20));
    if (jsonHas(m, "frame_timeout_ms")) o.frameTimeoutMs = jsonInt(m, "frame_timeout_ms", 300);
    if (jsonHas(m, "max_pending_frames"))
        o.maxPendingFrames = static_cast<std::size_t>(jsonInt(m, "max_pending_frames", 48));
    if (jsonHas(m, "au_queue_depth"))
        o.auQueueDepth = static_cast<std::size_t>(jsonInt(m, "au_queue_depth", 64));
    if (jsonHas(m, "bind_host")) o.bindHost = jsonStr(m, "bind_host", o.bindHost);
    if (jsonHas(m, "port")) o.port = static_cast<uint16_t>(jsonInt(m, "port", o.port));
    if (jsonHas(m, "codec_mode")) {
        VideoCodecMode mode;
        if (!videoCodecModeParse(jsonStr(m, "codec_mode", "auto").c_str(), &mode))
            return fail("无法识别的解码格式模式（可选 auto/h265/h264/jpeg）");
        o.codecMode = mode;
    }
    h->videoReceiver->setOptions(o);
    return 0;
}

int rm_video_receiver_set_codec_mode(rm_handle handle, const char *mode) {
    auto *h = asReceiver(handle);
    if (!h) return 1;
    VideoCodecMode m;
    if (!videoCodecModeParse(mode, &m))
        return fail(std::string("无法识别的解码格式模式: ") + (mode ? mode : "(null)") +
                    "（可选 auto/h265/h264/jpeg）");
    h->videoReceiver->setCodecMode(m);
    return 0;
}

const char *rm_video_receiver_codec_mode(rm_handle handle) {
    thread_local std::string buf;
    auto *h = static_cast<RmHandle *>(handle);
    buf = (h && h->videoReceiver) ? videoCodecModeName(h->videoReceiver->codecMode()) : "auto";
    return buf.c_str();
}

int rm_video_receiver_get_frame(rm_handle handle, uint8_t *bgr, int buflen, int *width,
                                int *height, int *stride, uint64_t *frame_no, int *keyframe,
                                uint64_t *since) {
    auto *h = asReceiver(handle);
    if (!h) return -1;
    if (!bgr || !since) return fail("get_frame 需要缓冲区与 since 指针"), -1;
    std::vector<uint8_t> tmp;
    VideoFrame info{};
    if (!h->videoReceiver->getLatestFrame(since, &tmp, &info)) return 0;
    if (static_cast<int>(tmp.size()) > buflen) return fail("缓冲区太小，放不下这一帧"), -1;
    std::memcpy(bgr, tmp.data(), tmp.size());
    if (width) *width = info.width;
    if (height) *height = info.height;
    if (stride) *stride = info.stride;
    if (frame_no) *frame_no = info.frameNumber;
    if (keyframe) *keyframe = info.keyframe ? 1 : 0;
    return 1;
}

void rm_video_receiver_set_frame_cb(rm_handle handle, rm_video_frame_cb cb, void *user) {
    auto *h = static_cast<RmHandle *>(handle);
    if (!h || !h->videoReceiver) return;
    if (!cb) {
        h->videoReceiver->setFrameHandler(nullptr);
        return;
    }
    // 回调可能来自解码线程，直接转调；user 指针由调用方保证生命周期
    h->videoReceiver->setFrameHandler([cb, user](const VideoFrame &f) {
        cb(user, f.data, f.width, f.height, f.stride, f.frameNumber, f.keyframe ? 1 : 0);
    });
}

int rm_video_receiver_stats(rm_handle handle, char *buf, int buflen) {
    auto *h = asReceiver(handle);
    if (!h) return -1;
    return copyOut(videoStatsToJson(h->videoReceiver->stats()), buf, buflen);
}

} // extern "C"
