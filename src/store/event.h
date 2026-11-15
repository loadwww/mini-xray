#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

namespace mxr {

// 一条事件 = events.jsonl 里的一行。
//
// 一次请求产生**两条**：type="req"（请求进来时）和 type="resp"（响应回去时），
// 靠 hop 配对。M5 就是靠这些行重建执行链的，所以这份 schema 定下来之后
// 尽量别改 —— 改了等于让所有历史 runs 的解析器失效。
//
// 为什么不复用"请求对象"直接 dump 整个 httplib::Request：
//   那里面混着连接信息（remote_addr、socket 句柄）、内部字段，还有一堆
//   转发出去其实不带的东西。事件是**给别人看的稳定接口**，字段少而明确。
struct Event {
    std::string type;      // "req" / "resp"（M4 会多出 "sse_chunk" / "sse_end"）
    std::string hop;       // "h-7"：同一次请求的两条记录共用这个值
    long long   seq = 0;   // Recorder 在入队时统一分配 → 文件里的顺序就是 seq 递增
    long long   tsMs = 0;  // 事件发生的墙上时间（毫秒，system_clock）
    std::string method;
    std::string path;      // 客户端原始 target（含 query）
    std::string upstream;  // req：最终打出去的完整 URL；resp 留空
    int         status = 0;
    long long   latencyMs = -1;    // 只有 resp 有值
    std::map<std::string, std::string> headers;   // 记的是"实际转发出去的那几个"，已脱敏
    std::string body;      // 已脱敏、已截断
    // sse_chunk：脱敏后、截断前的**原始 SSE 字节**（可能半行、可能含多个事件）
    // sse_end：留空

    // ── M4：流式事件专用字段 ────────────────────────────────────────
    // 为什么是"加字段"而不是"另起一种结构"：req/resp 的形状一个键都不能动
    // （老 runs 的解析器还认着它），新事件类型只**多**带几个键。
    // toJson 会按 type 条件输出，所以 req/resp 仍是原来的 11 个键。
    //
    // 字段为什么这么拆：
    //   chunk_index —— 流内序号（0,1,2…）。和 seq 不是一回事：seq 是
    //                 Recorder 给**整个文件**分配的行号，中间夹着别的请求、
    //                 别的 hop；replay 检查"chunk 顺序与到达顺序一致"要用流内序号。
    //   text / usage / duration_ms / bad_data —— 只在 sse_end 里出现一次，
    //                 是"这次流的最终结论"。replay 拿它和重新喂出来的结果对账。
    //   为什么 sse_chunk 只存原始字节、不存解析后的字段：
    //                 落盘要快、要能容忍半行；解析是 replay 的事。
    //                 存结论就没法验证解析器了（等于自己抄自己的答案）。
    long long   chunkIndex = -1;   // sse_chunk：流内第几块
    std::string text;              // sse_end：累积文本
    long long   promptTokens = 0;
    long long   completionTokens = 0;
    long long   totalTokens = 0;
    long long   durationMs = -1;   // sse_end：流开始 → 结束的耗时
    int         badDataCount = 0;  // sse_end：坏块数
};

// 序列化成一行 JSON（不含换行符，换行由 JsonlWriter 负责）
nlohmann::json toJson(const Event& ev);

} // namespace mxr
