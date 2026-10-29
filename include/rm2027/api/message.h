// RM2027 自定义终端协议 V1.0.0 —— 对外 API：消息基类与公共工具
//
// 所有指令结构体定义在 rm2027/api/messages.h（由工具从官方 PDF 字段表生成）。
// 发送与回调统一用 std::unique_ptr<具体结构体>（移动语义）。
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace rm2027 {

// 所有指令结构体的基类。派生类提供 static constexpr const char *kName。
struct Message {
    virtual ~Message() = default;
    virtual const char *name() const = 0;
};

enum class Direction { ClientToServer, ServerToClient };

const char *directionName(Direction d);

// ---------------------------------------------------------------------------
// 消息表查询
// ---------------------------------------------------------------------------
bool isKnownMessage(const std::string &name);
Direction messageDirection(const std::string &name);

std::vector<std::string> allMessageNames();   // 32 条
std::vector<std::string> uplinkNames();       // 客户端 -> 服务器，9 条
std::vector<std::string> downlinkNames();     // 服务器 -> 客户端，23 条

// 按名字创建一个空的指令结构体；未知名字返回 nullptr
std::unique_ptr<Message> createMessage(const std::string &name);

// ---------------------------------------------------------------------------
// 序列化 / 反序列化（供调试、落盘、测试使用；正常收发无需手工调用）
// ---------------------------------------------------------------------------
std::string toBinary(const Message &msg);
std::unique_ptr<Message> fromBinary(const std::string &name, const void *data, std::size_t size,
                                    std::string *error = nullptr);

// 规范化 JSON：camelCase、含默认值、bytes 用 base64
std::string toJson(const Message &msg);
std::unique_ptr<Message> fromJson(const std::string &name, const std::string &json,
                                  std::string *error = nullptr);

} // namespace rm2027
