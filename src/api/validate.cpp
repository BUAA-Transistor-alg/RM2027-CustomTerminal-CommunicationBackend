#include "rm2027/api/validate.h"

#include "rm2027/api/messages.h"

namespace rm2027 {

namespace {
bool fail(std::string *error, const std::string &why) {
    if (error != nullptr) *error = why;
    return false;
}

std::string sizeError(const char *what, std::size_t got, std::size_t want) {
    return std::string(what) + " 长度 " + std::to_string(got) + "，官方要求 " + std::to_string(want) + "";
}
} // namespace

bool validateMessage(const Message &msg, std::string *error) {
    if (const auto *m = dynamic_cast<const CustomControl *>(&msg)) {
        if (m->data.size() > kCustomControlMaxBytes) {
            return fail(error, sizeError("CustomControl.data", m->data.size(),
                                         kCustomControlMaxBytes) +
                                   " 字节以内（官方 §2.2.2，图传 0x0311 载荷上限）");
        }
        return true;
    }
    if (const auto *m = dynamic_cast<const CustomByteBlock *>(&msg)) {
        if (m->data.size() > kCustomByteBlockMaxBytes) {
            return fail(error, sizeError("CustomByteBlock.data", m->data.size(),
                                         kCustomByteBlockMaxBytes) +
                                   " 字节以内（官方 §2.2.20，2.4 kbit，图传 0x0310 载荷）");
        }
        return true;
    }
    if (const auto *m = dynamic_cast<const MapClickInfo *>(&msg)) {
        if (m->robot_id.size() != kMapClickRobotIdBytes) {
            return fail(error, "MapClickInfo.robot_id 长度 " + std::to_string(m->robot_id.size()) +
                                   "，官方要求固定 7 字节（不足补 0，官方 §2.2.17）");
        }
        return true;
    }
    if (const auto *m = dynamic_cast<const MapClickCmd *>(&msg)) {
        if (m->robot_id.size() != kMapClickRobotIdBytes) {
            return fail(error, "MapClickCmd.robot_id 长度 " + std::to_string(m->robot_id.size()) +
                                   "，官方要求固定 7 字节（不足补 0，官方 §2.2.18）");
        }
        return true;
    }
    if (const auto *m = dynamic_cast<const KeyboardMouseControl *>(&msg)) {
        if (m->keyboard_value > 0xFFFFu) {
            return fail(error, "KeyboardMouseControl.keyboard_value = " +
                                   std::to_string(m->keyboard_value) +
                                   "，官方只使用 bit 0~15（官方 §2.2.1）");
        }
        return true;
    }
    if (const auto *m = dynamic_cast<const GlobalUnitStatus *>(&msg)) {
        if (!m->robot_health.empty() && m->robot_health.size() != kGlobalUnitRobotHealthCount) {
            return fail(error, "GlobalUnitStatus.robot_health 长度 " +
                                   std::to_string(m->robot_health.size()) +
                                   "，官方要求 8 个（己方 1/3/4/7 + 对方 1/3/4/7，官方 §2.2.4）");
        }
        return true;
    }
    if (const auto *m = dynamic_cast<const RadarInfoToClient *>(&msg)) {
        if (!m->radar_single_robot_info.empty() &&
            m->radar_single_robot_info.size() != kRadarTargetCount) {
            return fail(error, "RadarInfoToClient.radar_single_robot_info 长度 " +
                                   std::to_string(m->radar_single_robot_info.size()) +
                                   "，官方要求 8 个目标（对方 1/3/4/7 + 己方 1/3/4/7，官方 §2.2.19）");
        }
        return true;
    }
    return true;
}

} // namespace rm2027
