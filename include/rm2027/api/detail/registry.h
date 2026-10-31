// RM2027 对外 API —— 内部消息注册表（由 tools/gen_api.py 生成的内容填表）
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "rm2027/api/message.h"

namespace rm2027 {

struct MessageDesc {
    const char *name;
    Direction direction;
    const google::protobuf::Descriptor *descriptor;

    // ---- 官方文档表 2-1 的发送频率 ----
    double default_hz;   // 0 表示「触发式发送」
    double max_hz;       // 触发式发送的频率上限；0 表示未规定
    int triggered;       // 1 = 触发式发送

    // ---- 字段表 JSON（供 GUI 自动生成表单）----
    const char *fields_json;
    // 原生结构体 -> protobuf 二进制（**当前线格式**，见 wire_format.h）
    std::function<bool(const Message &, std::string *)> encode;
    // 原生结构体 -> 规范（嵌套 message）线格式：JSON / 日志始终用这一种，可为空表示与 encode 相同
    std::function<bool(const Message &, std::string *)> encode_canonical;
    // protobuf 消息 -> 原生结构体；内容不合法时返回 nullptr
    std::function<std::unique_ptr<Message>(const google::protobuf::Message &)> decode;
    // 创建空的原生结构体
    std::function<std::unique_ptr<Message>()> create;
};

// 由 src/api/messages.cpp（生成）提供
const MessageDesc *messageTable(std::size_t *count);

// 按名字查表；未知返回 nullptr
const MessageDesc *findMessage(const std::string &name);

// 当前线格式下该消息对应的 protobuf 描述符。
// 只有 RadarInfoToClient 会随 rm2027::radarWireFormat() 变化，其余消息恒为 descriptor。
const google::protobuf::Descriptor *wireDescriptor(const MessageDesc &desc);

// 按**当前线格式**解析 MQTT payload；失败返回 nullptr 并写 error
std::unique_ptr<Message> fromWire(const MessageDesc &desc, const void *data, std::size_t size,
                                  std::string *error = nullptr);

} // namespace rm2027
