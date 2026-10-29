// RM2027 —— UDP 图传码流包头
//
// 官方协议（V1.0.0 §2）：
//   自定义终端可通过 udp 监听 3334 端口获取图传码流数据。图传码流数据的帧率为
//   60 帧，分辨率为 1920*1080，编码格式为 hevc，标准 h265 格式，无重传机制。
//   每个 udp 包的码流数据的前 8 个字节固定为：
//     帧编号（递增）       2 byte
//     当前帧内分片序号     2 byte
//     当前帧总字节数       4 byte
//   并且【此额外添加的 8 个字节为大端传输】。
//
// 所以本实现默认大端（可用 CMake 选项 -DRM_UDP_HEADER_BIG_ENDIAN=OFF 切到小端，
// 仅用于验证「字节序确实参与解析」，见 tests/check_endian.sh）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rm2027 {

struct UdpVideoHeader {
    uint16_t frameNumber{0};       // 帧编号（递增）
    uint16_t fragmentIndex{0};     // 当前帧内分片序号
    uint32_t frameTotalBytes{0};   // 当前帧总字节数
};

constexpr std::size_t kUdpVideoHeaderSize = 8;

// 编译期字节序选择
constexpr bool udpHeaderBigEndian() {
#if defined(RM_UDP_HEADER_BIG_ENDIAN) && RM_UDP_HEADER_BIG_ENDIAN
    return true;
#else
    return false;
#endif
}

const char *udpHeaderEndianName();

// 写入/读取 8 字节包头
void writeUdpVideoHeader(uint8_t out[kUdpVideoHeaderSize], const UdpVideoHeader &h);
UdpVideoHeader readUdpVideoHeader(const uint8_t in[kUdpVideoHeaderSize]);

// 数据包最大总长度（含包头）。1400 是常见以太网安全值（SharkDataSever 亦如此）。
constexpr std::size_t kDefaultMaxDatagram = 1400;
constexpr std::size_t kDefaultPayloadPerPacket = kDefaultMaxDatagram - kUdpVideoHeaderSize;

} // namespace rm2027
