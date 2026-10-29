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
    // 原生结构体 -> protobuf 二进制
    std::function<bool(const Message &, std::string *)> encode;
    // protobuf 消息 -> 原生结构体
    std::function<std::unique_ptr<Message>(const google::protobuf::Message &)> decode;
    // 创建空的原生结构体
    std::function<std::unique_ptr<Message>()> create;
};

// 由 src/api/messages.cpp（生成）提供
const MessageDesc *messageTable(std::size_t *count);

// 按名字查表；未知返回 nullptr
const MessageDesc *findMessage(const std::string &name);

} // namespace rm2027
