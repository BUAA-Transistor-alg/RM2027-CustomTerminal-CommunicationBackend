#include "rm2027/protocol/codec.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/util/json_util.h>

#include "rm2027/common/log.h"

namespace rm2027 {

namespace {
google::protobuf::util::JsonPrintOptions printOptions() {
    google::protobuf::util::JsonPrintOptions o;
    o.add_whitespace = false;
    // 关键：没有 presence 的字段（proto3 普通标量）即使为默认值也输出，
    // 这样「字段号写错 / 字段丢失」会直接表现为数值差异，而不会被静默忽略。
    o.always_print_primitive_fields = true;
    o.preserve_proto_field_names = false; // camelCase
    return o;
}
} // namespace

std::string encodeMessage(const google::protobuf::Message &msg) {
    std::string out;
    if (!msg.SerializeToString(&out)) {
        RM_ERROR("codec", "SerializeToString 失败: " << msg.GetTypeName());
        return std::string();
    }
    return out;
}

std::unique_ptr<google::protobuf::Message> decodeMessage(const google::protobuf::Descriptor *desc,
                                                          const void *data, std::size_t size,
                                                          std::string *error) {
    if (desc == nullptr) {
        if (error) *error = "描述符为空";
        return nullptr;
    }
    const google::protobuf::Message *proto =
        google::protobuf::MessageFactory::generated_factory()->GetPrototype(desc);
    if (proto == nullptr) {
        if (error) *error = "无法获取消息原型: " + desc->full_name();
        return nullptr;
    }
    std::unique_ptr<google::protobuf::Message> msg(proto->New());
    if (!msg->ParseFromArray(data, static_cast<int>(size))) {
        if (error) *error = "protobuf 解析失败: " + desc->name();
        return nullptr;
    }
    return msg;
}

std::string toCanonicalJson(const google::protobuf::Message &msg) {
    std::string out;
    const auto status = google::protobuf::util::MessageToJsonString(msg, &out, printOptions());
    if (!status.ok()) {
        RM_ERROR("codec", "MessageToJsonString 失败: " << status.ToString());
        return "{}";
    }
    return out;
}

std::unique_ptr<google::protobuf::Message> fromJson(const google::protobuf::Descriptor *desc,
                                                    const std::string &json, std::string *error) {
    if (desc == nullptr) {
        if (error) *error = "描述符为空";
        return nullptr;
    }
    const google::protobuf::Message *proto =
        google::protobuf::MessageFactory::generated_factory()->GetPrototype(desc);
    if (proto == nullptr) {
        if (error) *error = "无法获取消息原型: " + desc->full_name();
        return nullptr;
    }
    std::unique_ptr<google::protobuf::Message> msg(proto->New());
    const auto status = google::protobuf::util::JsonStringToMessage(json, msg.get());
    if (!status.ok()) {
        if (error) *error = status.ToString();
        return nullptr;
    }
    return msg;
}

std::string makeRecordJson(const std::string &direction, const std::string &topic,
                           std::size_t payloadSize, const std::string &canonicalData,
                           const std::string &extraFields) {
    std::string out = "{\"ts\":\"" + nowIso8601() + "\",\"dir\":\"" + direction + "\",\"topic\":\"" +
                      topic + "\",\"size\":" + std::to_string(payloadSize) + ",\"data\":" +
                      canonicalData;
    if (!extraFields.empty()) {
        out += ",";
        out += extraFields;
    }
    out += "}";
    return out;
}

} // namespace rm2027
