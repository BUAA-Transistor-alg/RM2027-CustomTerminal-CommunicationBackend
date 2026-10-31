// RM2027 自定义终端协议 —— 可切换的线格式
//
// 目前只有 RadarInfoToClient 有第二种合法解释（见 README「雷达线格式」）：
//   * NestedMessage   （默认）repeated RadarSingleRobotInfo（嵌套 message），
//                      当前主实现，与 ACE / ClearWei 等第三方实现的线格式一致；
//   * FlatPackedUint32        严格按官方 PDF §2.2.19 的字面声明
//                      `repeated uint32 RadarSingleRobotInfo;`（补字段号 = 1）：
//                      字段 1 为 packed repeated uint32，8 目标 × 3 值 = 24 个 uint32。
//
// 该开关是**进程级全局**设置（发送与接收共用），线程安全。MQTT topic / qos /
// 其它 31 条消息均不受影响；JSON / 日志始终使用嵌套形式（与线格式无关）。
#pragma once

namespace rm2027 {

enum class RadarWireFormat : int {
    NestedMessage = 0,   // 默认
    FlatPackedUint32 = 1 // 官方 PDF 字面
};

// 当前线格式（默认 NestedMessage）
RadarWireFormat radarWireFormat();

// 设置线格式；发送前设置即可，接收端必须与发送端一致
void setRadarWireFormat(RadarWireFormat format);

// "nested" / "flat"
const char *radarWireFormatName(RadarWireFormat format);

// 解析 "nested" / "flat"（也接受 "0" / "1"）；成功返回 true
bool parseRadarWireFormat(const char *text, RadarWireFormat *out);

} // namespace rm2027
