// RM2027 自定义终端协议 —— 官方 §2.2 字段长度 / 值域约束校验
//
// 这些约束写在官方 PDF §2.2 的字段表/正文里（proto 本身无法表达），
// 发送前由 Server::send() / Client::send() 调用；不符合时发送被拒绝并给出原因。
// 接收端不做校验（对端数据一律尽量解析）。
#pragma once

#include <cstddef>
#include <string>

#include "rm2027/api/message.h"

namespace rm2027 {

// 官方 §2.2.2：CustomControl.data 最大 60 字节（图传 0x0311 载荷）
constexpr std::size_t kCustomControlMaxBytes = 60;
// 官方 §2.2.20：CustomByteBlock.data 最大 2.4 kbit = 300 字节（图传 0x0310 载荷）
constexpr std::size_t kCustomByteBlockMaxBytes = 300;
// 官方 §2.2.17/2.2.18：MapClick robot_id 固定 7 字节
constexpr std::size_t kMapClickRobotIdBytes = 7;
// 官方 §2.2.4：GlobalUnitStatus.robot_health 己方 1/3/4/7 + 对方 1/3/4/7 = 8 个
constexpr std::size_t kGlobalUnitRobotHealthCount = 8;
// 官方 §2.2.19：RadarInfoToClient 每方 1/3/4/7 号，8 个目标
constexpr std::size_t kRadarTargetCount = 8;

// 返回 true = 符合官方约束；false 时把原因写入 error（可为空）
bool validateMessage(const Message &msg, std::string *error = nullptr);

} // namespace rm2027
