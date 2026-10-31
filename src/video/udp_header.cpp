#include "rm2027/video/udp_header.h"

namespace rm2027 {

namespace {
// 官方 2027 通信协议 V1.0.0 §2 明文规定：8 字节包头为大端传输。
// 这里固定按大端读写，没有编译期开关、也没有运行期切换。
inline void put16(uint8_t *p, uint16_t v) {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}
inline void put32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}
inline uint16_t get16(const uint8_t *p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline uint32_t get32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
} // namespace

void writeUdpVideoHeader(uint8_t out[kUdpVideoHeaderSize], const UdpVideoHeader &h) {
    put16(out + 0, h.frameNumber);
    put16(out + 2, h.fragmentIndex);
    put32(out + 4, h.frameTotalBytes);
}

UdpVideoHeader readUdpVideoHeader(const uint8_t in[kUdpVideoHeaderSize]) {
    UdpVideoHeader h;
    h.frameNumber = get16(in + 0);
    h.fragmentIndex = get16(in + 2);
    h.frameTotalBytes = get32(in + 4);
    return h;
}

} // namespace rm2027
