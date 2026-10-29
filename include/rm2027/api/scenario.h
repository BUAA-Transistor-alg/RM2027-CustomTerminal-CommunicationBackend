// RM2027 —— 测试场景文件加载
//
// 场景文件格式（见 tests/scenarios/*.json）：
//   { "steps": [ { "topic": "GameStatus", "data": { ... } }, ... ] }
//
// 加载后每步就是「指令名 + 该指令内容的 JSON 文本（snake_case 字段）」，
// 可直接交给 Client::send() / Server::send()。
#pragma once

#include <string>
#include <vector>

namespace rm2027 {

struct ScenarioStep {
    std::string topic;
    std::string dataJson;   // 原始 JSON 对象文本
};

class Scenario {
public:
    // 读取并解析场景文件；失败时返回 false 并填 error
    static bool load(const std::string &path, Scenario *out, std::string *error);

    const std::vector<ScenarioStep> &steps() const { return steps_; }
    bool empty() const { return steps_.empty(); }
    std::size_t size() const { return steps_.size(); }

private:
    std::vector<ScenarioStep> steps_;
};

} // namespace rm2027
