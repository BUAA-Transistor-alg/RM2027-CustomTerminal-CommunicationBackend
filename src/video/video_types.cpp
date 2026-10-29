#include "rm2027/video/video_types.h"

#include <cstdio>
#include <sstream>

namespace rm2027 {

const char *videoCodecModeName(VideoCodecMode m) {
    switch (m) {
    case VideoCodecMode::H265: return "h265";
    case VideoCodecMode::H264: return "h264";
    case VideoCodecMode::Jpeg: return "jpeg";
    default: return "auto";
    }
}

VideoCodec videoCodecModeCodec(VideoCodecMode m) {
    switch (m) {
    case VideoCodecMode::H265: return VideoCodec::H265;
    case VideoCodecMode::H264: return VideoCodec::H264;
    case VideoCodecMode::Jpeg: return VideoCodec::Jpeg;
    default: return VideoCodec::Unknown;
    }
}

bool videoCodecModeParse(const char *text, VideoCodecMode *out) {
    if (!text || !out) return false;
    std::string t(text);
    for (char &c : t) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    // 去掉空白与点、减号，便于接受 "H.265" / "h-265"
    std::string k;
    for (char c : t) {
        if (c == ' ' || c == '\t' || c == '.' || c == '-' || c == '_') continue;
        k += c;
    }
    if (k == "auto" || k == "" || k == "probe" || k == "detect") *out = VideoCodecMode::Auto;
    else if (k == "h265" || k == "hevc" || k == "265") *out = VideoCodecMode::H265;
    else if (k == "h264" || k == "avc" || k == "264") *out = VideoCodecMode::H264;
    else if (k == "jpeg" || k == "mjpeg" || k == "jpg") *out = VideoCodecMode::Jpeg;
    else return false;
    return true;
}

std::string videoStatsToJson(const VideoStats &s) {
    std::ostringstream o;
    o << "{";
    o << "\"running\":" << (s.running ? "true" : "false");
    o << ",\"codec\":\"" << videoCodecName(s.codec) << "\"";
    o << ",\"codecMode\":\"" << videoCodecModeName(s.codecMode) << "\"";
    o << ",\"width\":" << s.width << ",\"height\":" << s.height;
    o << ",\"waitingKeyframe\":" << (s.waitingKeyframe ? "true" : "false");
    o << ",\"tx\":{\"frames\":" << s.framesSent << ",\"packets\":" << s.packetsSent
      << ",\"bytes\":" << s.bytesSent << ",\"encodeErrors\":" << s.encodeErrors
      << ",\"fps\":" << s.txFps << ",\"mbps\":" << s.txMbps << "}";
    o << ",\"rx\":{\"packets\":" << s.packetsReceived << ",\"bytes\":" << s.bytesReceived
      << ",\"framesComplete\":" << s.framesComplete << ",\"framesIncomplete\":" << s.framesIncomplete
      << ",\"framesAbandoned\":" << s.framesAbandoned
      << ",\"duplicateFragments\":" << s.duplicateFragments
      << ",\"outOfOrderFrames\":" << s.out_of_order_frames << ",\"badPackets\":" << s.badPackets << ",\"frameGaps\":" << s.frameGaps
      << ",\"fps\":" << s.rxFps << ",\"mbps\":" << s.rxMbps << "}";
    o << ",\"decode\":{\"frames\":" << s.framesDecoded << ",\"errors\":" << s.decodeErrors
      << ",\"droppedBySlot\":" << s.framesDroppedBySlot << ",\"keyframeWaits\":" << s.keyframeWaits
      << ",\"decoderResets\":" << s.decoderResets << ",\"fps\":" << s.decodeFps << "}";
    o << "}";
    return o.str();
}

} // namespace rm2027
