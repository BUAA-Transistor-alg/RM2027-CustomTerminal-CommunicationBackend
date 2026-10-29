#include "rm2027/common/log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace rm2027 {

namespace {
std::atomic<int> g_level{static_cast<int>(LogLevel::Info)};
std::mutex g_outMutex;
std::mutex g_errMutex;

const char *levelName(LogLevel lv) {
    switch (lv) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Error: return "ERROR";
    }
    return "?????";
}
} // namespace

void setLogLevel(LogLevel lv) { g_level.store(static_cast<int>(lv)); }
LogLevel logLevel() { return static_cast<LogLevel>(g_level.load()); }
bool logEnabled(LogLevel lv) { return static_cast<int>(lv) >= g_level.load(); }

void logMsg(LogLevel lv, const std::string &tag, const std::string &text) {
    std::lock_guard<std::mutex> lk(g_errMutex);
    std::fprintf(stderr, "[%s] %-6s %-9s %s\n", nowIso8601().c_str(), levelName(lv), tag.c_str(),
                 text.c_str());
    std::fflush(stderr);
}

void emitRecord(const std::string &json) {
    std::lock_guard<std::mutex> lk(g_outMutex);
    std::fwrite(json.data(), 1, json.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

std::string nowIso8601() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto secs = time_point_cast<seconds>(now);
    const auto ms = duration_cast<milliseconds>(now - secs).count();
    const std::time_t t = system_clock::to_time_t(secs);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    char buf[80];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03d", tmv.tm_year + 1900,
                  tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                  static_cast<int>(ms));
    return std::string(buf);
}

} // namespace rm2027
