#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace mxr {

// ═══════════════════════════════════════════════════════════════════
//  执行图的数据模型：Run / Step / ToolExec
// ═══════════════════════════════════════════════════════════════════
//
// 【这层是干嘛的】
//
//   M4 落盘的是"发生了哪些事件"（req / resp / sse_chunk / sse_end），
//   M5 要还原的是"agent 做了什么"：一次任务 → 若干轮模型调用 → 每轮的工具执行。
//   本文件定义还原结果的**稳定形状**，M6 出报告、M7 的 UI、replay 都读它。
//
// 【边界：这一层不认识什么】
//
//   不 include httplib、不 include Recorder、不 include SSE 解析器、不碰文件 I/O。
//   理由：模型是"结论"，不是"采集过程"。一旦它依赖 httplib::Request，
//   离线 replay（只读 events.jsonl）就得把整个 HTTP 库拖进来，单元测试也得
//   造一个假 Request 才能测一次往返。纯数据结构 + 序列化 = 到处都能用。
//
// 【三层为什么这么拆】
//
//   Run      = 一次用户任务（一串递增的 messages 前缀）
//   Step     = 一轮模型调用（一条 assistant 回复 + 它发起的工具调用）
//   ToolExec = 一次工具执行（参数、结果、耗时、失败等级）
//
//   为什么不把工具直接摊平成 Run 下的列表：并行工具属于**同一轮**，
//   T5.4 归并时要按 Step 取"并行区间的最大值"，摊平就丢了这个归属。

enum class ErrorKind {
    none,       // 没有失败迹象
    inferred,   // 文本里像失败了（启发式，可能误报）
    confirmed   // 协议层明确说失败了（is_error / error 字段）
};

// token 用量。observed 是 T5.2 的关键：
//   "没看到 usage" 和 "usage 真的是 0" 是两件不同的事，
//   都存成 0 的话，M6 的平均耗时/成本统计会把"未观测"当成"零消耗"。
struct Usage {
    long long promptTokens = 0;
    long long completionTokens = 0;
    long long totalTokens = 0;
    bool observed = false;
};

struct ToolExec {
    // T5.4 要靠 tool_call_id 把后续的 role:"tool" 结果回填给正确的调用
    std::string toolCallId;
    std::string name;        // 工具名
    std::string argsJson;    // 原始参数 JSON 文本
    std::string resultJson;  // 原始结果 JSON 文本
    long long startMs = 0;
    long long durationMs = 0;
    ErrorKind error = ErrorKind::none;
    bool durationInferred = false; // 从流量推断的耗时，UI 上要显示 "~"
};

struct Step {
    int index = 0;                     // 在 Run 内的序号
    long long startMs = 0;
    long long endMs = 0;
    std::string model;
    std::string requestFingerprint;   // T5.3 建链用的 messages 前缀滚动哈希
    std::vector<ToolExec> tools;
    std::string outputText;
    Usage usage;
    bool complete = false;            // false = 流被杀 / 客户端断开，没等到收尾
};

struct Run {
    std::string id;                   // 稳定 id：时间 + 根哈希 + 序号
    std::vector<Step> steps;
    long long startMs = 0;
    long long endMs = 0;
};

// 为什么 argsJson / resultJson 存字符串而不是 nlohmann::json 对象：
//   工具参数和结果的形状千变万化，模型层不该替上游规定结构；
//   而且"忠实记录原文"才能让 M5 的解析错误在事后被复查出来。

// ── 序列化：nlohmann 的 ADL 自由函数（与类型同 namespace）──────────
//   json j = run;        会去找 to_json(j, run)
//   Run r = j.get<Run>(); 会去找 from_json(j, r)
void to_json(nlohmann::json &j, const Usage &v);
void from_json(const nlohmann::json &j, Usage &v);

void to_json(nlohmann::json &j, const ToolExec &v);
void from_json(const nlohmann::json &j, ToolExec &v);

void to_json(nlohmann::json &j, const Step &v);
void from_json(const nlohmann::json &j, Step &v);

void to_json(nlohmann::json &j, const Run &v);
void from_json(const nlohmann::json &j, Run &v);

// C++17 没有默认 operator==，往返一致要自己逐个字段比
bool operator==(const Usage &a, const Usage &b);
bool operator==(const ToolExec &a, const ToolExec &b);
bool operator==(const Step &a, const Step &b);
bool operator==(const Run &a, const Run &b);

// 枚举转字符串：JSON 里写 "confirmed" 而不是数字 2
const char *toString(ErrorKind k);
ErrorKind errorKindFromString(const std::string &s);

} // namespace mxr
