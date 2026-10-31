#include "rm2027/api/wire_format.h"

#include <atomic>
#include <cctype>
#include <cstring>
#include <string>

namespace rm2027 {

namespace {
std::atomic<int> g_radarWireFormat{static_cast<int>(RadarWireFormat::NestedMessage)};
} // namespace

RadarWireFormat radarWireFormat() {
    return static_cast<RadarWireFormat>(g_radarWireFormat.load(std::memory_order_relaxed));
}

void setRadarWireFormat(RadarWireFormat format) {
    g_radarWireFormat.store(static_cast<int>(format), std::memory_order_relaxed);
}

const char *radarWireFormatName(RadarWireFormat format) {
    return format == RadarWireFormat::FlatPackedUint32 ? "flat" : "nested";
}

bool parseRadarWireFormat(const char *text, RadarWireFormat *out) {
    if (text == nullptr || out == nullptr) return false;
    std::string v(text);
    for (char &c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (v == "nested" || v == "nested_message" || v == "0") {
        *out = RadarWireFormat::NestedMessage;
        return true;
    }
    if (v == "flat" || v == "flat_packed_uint32" || v == "packed" || v == "1") {
        *out = RadarWireFormat::FlatPackedUint32;
        return true;
    }
    return false;
}

} // namespace rm2027
