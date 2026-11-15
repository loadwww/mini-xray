#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "model/model.h"
#include "parse/extract.h"

namespace mxr {

// ═══════════════════════════════════════════════════════════════════
//  建链：把一堆 HTTP 事件还原成若干条 agent 执行链
// ═══════════════════════════════════════════════════════════════════
//
// 【这层是干嘛的】
//
//   events.jsonl 里是一堆扁平的 req / resp / sse_chunk / sse_end，
//   看不出"哪些请求属于同一个任务"。agent 循环的形态是：
//     模型输出 → 客户端执行工具 → 把结果追加进 messages 再发一次
//   所以**一次 run = 一串递增的 messages 前缀**。本层靠这个事实把请求串起来。
//
// 【边界】
//
//   只吃"已经配对好的 Hop 列表"，不碰 HTTP、不碰 socket、不写文件。
//   读 events.jsonl 由 readEvents 负责，字段提取由 T5.2 的 extract 负责。
//
// 【为什么用哈希而不是全量比较 messages】
//
//   O(n²) 逐条比文本要反复扫过整段历史（含 system prompt、长工具结果）；
//   滚动前缀哈希把"前 k 条消息"压成一个短串，判前缀只需比一次。
//   代价是**碰撞**：理论上两个不同序列可能得到同一个哈希。
//   本关同时保留 isStrictPrefix 全量版作为对照，验证台会断言两者结果一致。

// 一条配对好的请求-响应。流式请求的 resp.body 是空的，结论在 sse_end 里。
struct Hop {
    std::string hop;
    long long seq = 0;          // req 的 seq，决定时间顺序
    long long startMs = 0;      // req.ts_ms
    long long endMs = 0;        // resp / sse_end 的 ts_ms
    std::string method;
    std::string path;
    int status = 0;
    RequestInfo request;
    ResponseInfo response;
};

struct PairResult {
    std::vector<Hop> hops;
    int orphanResponses = 0;    // 有 resp 但没有 req 的孤儿
};

// 读 events.jsonl：一行一个 JSON，坏行跳过（不抛）
std::vector<nlohmann::json> readEvents(const std::string &path);

// 按 hop 把 req / resp 配成对，按 seq 升序。
// 流式 hop 用 sse_end 补 response，非流式用 resp.body 补。
PairResult pairHops(const std::vector<nlohmann::json> &events);

// 单条消息的稳态哈希（role/content/tool_call_id/name 一起算）
std::string messageHash(const Message &m);

// 把一条消息 chaining 到前一个哈希上（滚动前缀哈希的单步）
std::string chainHash(const std::string &prev, const Message &m);

// prefixHashes(messages)[i] = 前 i+1 条消息的链哈希；back() 即整条链指纹
std::vector<std::string> prefixHashes(const std::vector<Message> &messages);

// 朴素全量版：a 是不是 b 的严格前缀（O(n) 文本比较，用于对照）
bool isStrictPrefix(const std::vector<Message> &a, const std::vector<Message> &b);

// 稳定 run id：根事件时间 + 根指纹 + 序号。不用 nowMs()，否则重放时 id 会变
std::string makeRunId(long long rootTsMs, const std::string &rootHash, int index);

// 建链：返回按根分组的 Run 列表
std::vector<Run> buildRuns(const std::vector<Hop> &hops);

} // namespace mxr
