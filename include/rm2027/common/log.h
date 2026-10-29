// RM2027 自定义终端 —— 日志
//
// 约定：
//   * 人类可读日志 -> stderr
//   * 结构化 JSON 记录 -> stdout（供自动化验证脚本解析）
#pragma once

#include <sstream>
#include <string>

namespace rm2027 {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

void setLogLevel(LogLevel lv);
LogLevel logLevel();
bool logEnabled(LogLevel lv);

// 写一条人类可读日志到 stderr
void logMsg(LogLevel lv, const std::string &tag, const std::string &text);

// 写一行结构化 JSON 到 stdout 并 flush（验证脚本按行读取）
void emitRecord(const std::string &json);

std::string nowIso8601();

} // namespace rm2027

#define RM_LOG(level, tag, expr)                                                                   \
    do {                                                                                           \
        if (::rm2027::logEnabled(level)) {                                                         \
            std::ostringstream _rm_os;                                                             \
            _rm_os << expr;                                                                        \
            ::rm2027::logMsg(level, tag, _rm_os.str());                                            \
        }                                                                                          \
    } while (0)

#define RM_DEBUG(tag, expr) RM_LOG(::rm2027::LogLevel::Debug, tag, expr)
#define RM_INFO(tag, expr) RM_LOG(::rm2027::LogLevel::Info, tag, expr)
#define RM_WARN(tag, expr) RM_LOG(::rm2027::LogLevel::Warn, tag, expr)
#define RM_ERROR(tag, expr) RM_LOG(::rm2027::LogLevel::Error, tag, expr)
