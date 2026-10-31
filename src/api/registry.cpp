// RM2027 对外 API —— 消息表查询与序列化的实现
#include <algorithm>
#include <cstring>

#include "rm2027/api/detail/registry.h"
#include "rm2027/api/message.h"
#include "rm2027/api/wire_format.h"
#include "rm2027/protocol/codec.h"
#include "rm2027_radar_flat.pb.h"

namespace rm2027 {

const char *directionName(Direction d) {
    return d == Direction::ClientToServer ? "up" : "down";
}

const MessageDesc *findMessage(const std::string &name) {
    std::size_t count = 0;
    const MessageDesc *table = messageTable(&count);
    for (std::size_t i = 0; i < count; ++i) {
        if (name == table[i].name) return &table[i];
    }
    return nullptr;
}

bool isKnownMessage(const std::string &name) { return findMessage(name) != nullptr; }

Direction messageDirection(const std::string &name) {
    const MessageDesc *d = findMessage(name);
    return d ? d->direction : Direction::ServerToClient;
}

namespace {
std::vector<std::string> namesOf(Direction dir) {
    std::size_t count = 0;
    const MessageDesc *table = messageTable(&count);
    std::vector<std::string> v;
    v.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (table[i].direction == dir) v.push_back(table[i].name);
    }
    return v;
}
} // namespace

std::vector<std::string> allMessageNames() {
    std::size_t count = 0;
    const MessageDesc *table = messageTable(&count);
    std::vector<std::string> v;
    v.reserve(count);
    for (std::size_t i = 0; i < count; ++i) v.push_back(table[i].name);
    return v;
}

std::vector<std::string> uplinkNames() { return namesOf(Direction::ClientToServer); }
std::vector<std::string> downlinkNames() { return namesOf(Direction::ServerToClient); }

std::unique_ptr<Message> createMessage(const std::string &name) {
    const MessageDesc *d = findMessage(name);
    return d ? d->create() : nullptr;
}

const google::protobuf::Descriptor *wireDescriptor(const MessageDesc &desc) {
    // 目前只有 RadarInfoToClient 存在两种线格式；其余消息恒用官方 proto 的定义。
    if (desc.name != nullptr && std::strcmp(desc.name, "RadarInfoToClient") == 0 &&
        radarWireFormat() == RadarWireFormat::FlatPackedUint32) {
        return rm_radar_flat::RadarInfoToClient::descriptor();
    }
    return desc.descriptor;
}

std::string toBinary(const Message &msg) {
    const MessageDesc *d = findMessage(msg.name());
    if (d == nullptr) return std::string();
    std::string out;
    if (!d->encode(msg, &out)) return std::string();
    return out;
}

std::unique_ptr<Message> fromWire(const MessageDesc &desc, const void *data, std::size_t size,
                                  std::string *error) {
    auto proto = decodeMessage(wireDescriptor(desc), data, size, error);
    if (!proto) return nullptr;
    auto native = desc.decode(*proto);
    if (!native && error) {
        *error = "内容不合法: " + std::string(desc.name) + "（" +
                 radarWireFormatName(radarWireFormat()) + " 线格式）";
    }
    return native;
}

std::unique_ptr<Message> fromBinary(const std::string &name, const void *data, std::size_t size,
                                    std::string *error) {
    const MessageDesc *d = findMessage(name);
    if (d == nullptr) {
        if (error) *error = "未知指令: " + name;
        return nullptr;
    }
    return fromWire(*d, data, size, error);
}

std::string toJson(const Message &msg) {
    const MessageDesc *d = findMessage(msg.name());
    if (d == nullptr) return "{}";
    // JSON / 日志始终使用**规范（嵌套 message）**形式，与当前线格式开关无关，
    // 这样同一个场景文件/记录格式在 nested 与 flat 两种线格式下完全一致。
    std::string bin;
    const bool ok = d->encode_canonical ? d->encode_canonical(msg, &bin) : d->encode(msg, &bin);
    if (!ok) return "{}";
    auto proto = decodeMessage(d->descriptor, bin.data(), bin.size());
    if (!proto) return "{}";
    return toCanonicalJson(*proto);
}

std::unique_ptr<Message> fromJson(const std::string &name, const std::string &json,
                                  std::string *error) {
    const MessageDesc *d = findMessage(name);
    if (d == nullptr) {
        if (error) *error = "未知指令: " + name;
        return nullptr;
    }
    // 场景/JSON 输入同样是规范（嵌套）形式；接收端解码才由线格式开关决定。
    auto proto = rm2027::fromJson(d->descriptor, json, error);
    if (!proto) return nullptr;
    return d->decode(*proto);
}

} // namespace rm2027
