#include "model/model.h"

namespace mxr {
namespace {

// 离线读文件用：缺键或类型不符都回落默认值，不让一份老/坏文件把解析搞崩。
template <typename T>
T readField(const nlohmann::json &j, const char *key, T fallback)
{
    if (!j.contains(key)) return fallback;
    try {
        return j.at(key).get<T>();
    } catch (const nlohmann::json::exception &) {
        return fallback;
    }
}

} // namespace

// 枚举写成字符串：JSON 是对外接口，写数字的话以后插入新等级会让历史文件语义错位。
const char *toString(ErrorKind k)
{
    switch (k) {
    case ErrorKind::none:      return "none";
    case ErrorKind::inferred:  return "inferred";
    case ErrorKind::confirmed: return "confirmed";
    }
    return "none";
}

ErrorKind errorKindFromString(const std::string &s)
{
    if (s == "inferred")  return ErrorKind::inferred;
    if (s == "confirmed") return ErrorKind::confirmed;
    return ErrorKind::none;
}

// 结构 → JSON。嵌套直接赋值，nlohmann 会递归找对应类型的 to_json。
void to_json(nlohmann::json &j, const Usage &v)
{
    j["prompt_tokens"] = v.promptTokens;
    j["completion_tokens"] = v.completionTokens;
    j["total_tokens"] = v.totalTokens;
    j["observed"] = v.observed;
}

void to_json(nlohmann::json &j, const ToolExec &v)
{
    j["tool_call_id"] = v.toolCallId;
    j["name"] = v.name;
    j["args_json"] = v.argsJson;
    j["result_json"] = v.resultJson;
    j["start_ms"] = v.startMs;
    j["duration_ms"] = v.durationMs;
    j["error"] = toString(v.error);
    j["duration_inferred"] = v.durationInferred;
}

void to_json(nlohmann::json &j, const Step &v)
{
    j["index"] = v.index;
    j["start_ms"] = v.startMs;
    j["end_ms"] = v.endMs;
    j["model"] = v.model;
    j["request_fingerprint"] = v.requestFingerprint;
    j["tools"] = v.tools;
    j["output_text"] = v.outputText;
    j["usage"] = v.usage;
    j["complete"] = v.complete;
}

void to_json(nlohmann::json &j, const Run &v)
{
    j["id"] = v.id;
    j["steps"] = v.steps;
    j["start_ms"] = v.startMs;
    j["end_ms"] = v.endMs;
}

// JSON → 结构。字段一律走 readField，缺省和类型错误都不抛。
void from_json(const nlohmann::json &j, Usage &v)
{
    v.promptTokens = readField<long long>(j, "prompt_tokens", 0LL);
    v.completionTokens = readField<long long>(j, "completion_tokens", 0LL);
    v.totalTokens = readField<long long>(j, "total_tokens", 0LL);
    v.observed = readField<bool>(j, "observed", false);
}

void from_json(const nlohmann::json &j, ToolExec &v)
{
    v.toolCallId = readField<std::string>(j, "tool_call_id", std::string());
    v.name = readField<std::string>(j, "name", std::string());
    v.argsJson = readField<std::string>(j, "args_json", std::string());
    v.resultJson = readField<std::string>(j, "result_json", std::string());
    v.startMs = readField<long long>(j, "start_ms", 0LL);
    v.durationMs = readField<long long>(j, "duration_ms", 0LL);
    v.error = errorKindFromString(readField<std::string>(j, "error", std::string("none")));
    v.durationInferred = readField<bool>(j, "duration_inferred", false);
}

void from_json(const nlohmann::json &j, Step &v)
{
    v.index = readField<int>(j, "index", 0);
    v.startMs = readField<long long>(j, "start_ms", 0LL);
    v.endMs = readField<long long>(j, "end_ms", 0LL);
    v.model = readField<std::string>(j, "model", std::string());
    v.requestFingerprint = readField<std::string>(j, "request_fingerprint", std::string());
    v.tools = readField<std::vector<ToolExec>>(j, "tools", std::vector<ToolExec>{});
    v.outputText = readField<std::string>(j, "output_text", std::string());
    v.usage = readField<Usage>(j, "usage", Usage{});
    v.complete = readField<bool>(j, "complete", false);
}

void from_json(const nlohmann::json &j, Run &v)
{
    v.id = readField<std::string>(j, "id", std::string());
    v.steps = readField<std::vector<Step>>(j, "steps", std::vector<Step>{});
    v.startMs = readField<long long>(j, "start_ms", 0LL);
    v.endMs = readField<long long>(j, "end_ms", 0LL);
}

// 往返一致性判据：每个字段都要比，漏一个就会让往返测试形同虚设。
bool operator==(const Usage &a, const Usage &b)
{
    return a.promptTokens == b.promptTokens
        && a.completionTokens == b.completionTokens
        && a.totalTokens == b.totalTokens
        && a.observed == b.observed;
}

bool operator==(const ToolExec &a, const ToolExec &b)
{
    return a.toolCallId == b.toolCallId
        && a.name == b.name
        && a.argsJson == b.argsJson
        && a.resultJson == b.resultJson
        && a.startMs == b.startMs
        && a.durationMs == b.durationMs
        && a.error == b.error
        && a.durationInferred == b.durationInferred;
}

bool operator==(const Step &a, const Step &b)
{
    return a.index == b.index
        && a.startMs == b.startMs
        && a.endMs == b.endMs
        && a.model == b.model
        && a.requestFingerprint == b.requestFingerprint
        && a.tools == b.tools
        && a.outputText == b.outputText
        && a.usage == b.usage
        && a.complete == b.complete;
}

bool operator==(const Run &a, const Run &b)
{
    return a.id == b.id
        && a.steps == b.steps
        && a.startMs == b.startMs
        && a.endMs == b.endMs;
}

} // namespace mxr
