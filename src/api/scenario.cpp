#include "rm2027/api/scenario.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace rm2027 {

namespace {

// 从 pos 处的 '{' 或 '[' 开始，返回与之配对的收尾字符之后的位置。
// 会正确跳过字符串字面量里的括号和转义。
std::size_t matchBracket(const std::string &s, std::size_t pos) {
    if (pos >= s.size()) return std::string::npos;
    const char open = s[pos];
    const char close = (open == '{') ? '}' : ']';
    int depth = 0;
    bool inStr = false;
    for (std::size_t i = pos; i < s.size(); ++i) {
        const char c = s[i];
        if (inStr) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == open) ++depth;
        else if (c == close) {
            if (--depth == 0) return i + 1;
        }
    }
    return std::string::npos;
}

// 找到 key（形如 "topic"）之后的值起点
std::size_t findValue(const std::string &s, const std::string &key, std::size_t from) {
    const std::string pat = "\"" + key + "\"";
    std::size_t p = s.find(pat, from);
    if (p == std::string::npos) return std::string::npos;
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return std::string::npos;
    ++p;
    while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
    return p;
}

std::string readJsonString(const std::string &s, std::size_t pos) {
    if (pos >= s.size() || s[pos] != '"') return {};
    std::string out;
    for (std::size_t i = pos + 1; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            // 保留转义原样（指令名里不会出现，稳妥起见还是照抄）
            out += s[i];
            out += s[i + 1];
            ++i;
            continue;
        }
        if (s[i] == '"') break;
        out += s[i];
    }
    return out;
}

} // namespace

bool Scenario::load(const std::string &path, Scenario *out, std::string *error) {
    if (!out) return false;
    std::ifstream f(path);
    if (!f) {
        if (error) *error = "打不开场景文件: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();

    const std::size_t stepsPos = findValue(text, "steps", 0);
    if (stepsPos == std::string::npos || stepsPos >= text.size() || text[stepsPos] != '[') {
        if (error) *error = "场景文件里找不到 \"steps\" 数组: " + path;
        return false;
    }

    out->steps_.clear();
    std::size_t i = stepsPos + 1;
    while (i < text.size()) {
        while (i < text.size() && (std::isspace(static_cast<unsigned char>(text[i])) ||
                                   text[i] == ',')) ++i;
        if (i >= text.size() || text[i] == ']') break;
        if (text[i] != '{') { ++i; continue; }

        const std::size_t end = matchBracket(text, i);
        if (end == std::string::npos) {
            if (error) *error = "场景文件 JSON 括号不配对: " + path;
            return false;
        }
        const std::string obj = text.substr(i, end - i);

        ScenarioStep step;
        const std::size_t tp = findValue(obj, "topic", 0);
        if (tp != std::string::npos) step.topic = readJsonString(obj, tp);
        const std::size_t dp = findValue(obj, "data", 0);
        if (dp != std::string::npos && dp < obj.size() && obj[dp] == '{') {
            const std::size_t de = matchBracket(obj, dp);
            if (de != std::string::npos) step.dataJson = obj.substr(dp, de - dp);
        }
        if (step.topic.empty()) {
            if (error) *error = "场景文件里有步骤缺少 topic: " + path;
            return false;
        }
        if (step.dataJson.empty()) step.dataJson = "{}";
        out->steps_.push_back(std::move(step));
        i = end;
    }

    if (out->steps_.empty()) {
        if (error) *error = "场景文件里没有任何步骤: " + path;
        return false;
    }
    return true;
}

} // namespace rm2027
