// RM2027 —— protobuf 编解码与规范化 JSON（内部使用）
#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace google {
namespace protobuf {
class Descriptor;
class Message;
} // namespace protobuf
} // namespace google

namespace rm2027 {

std::string encodeMessage(const google::protobuf::Message &msg);

std::unique_ptr<google::protobuf::Message>
decodeMessage(const google::protobuf::Descriptor *desc, const void *data, std::size_t size,
              std::string *error = nullptr);

// 规范化 JSON：camelCase 字段名、包含默认值、bytes 用 base64、按字段号排序
std::string toCanonicalJson(const google::protobuf::Message &msg);

std::unique_ptr<google::protobuf::Message>
fromJson(const google::protobuf::Descriptor *desc, const std::string &json,
         std::string *error = nullptr);

// 组装一行结构化记录（stdout，供自动化验证脚本解析）
std::string makeRecordJson(const std::string &direction, const std::string &topic,
                           std::size_t payloadSize, const std::string &canonicalData,
                           const std::string &extraFields = std::string());

} // namespace rm2027
