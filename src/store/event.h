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
};

// 序列化成一行 JSON（不含换行符，换行由 JsonlWriter 负责）
nlohmann::json toJson(const Event& ev);

} // namespace mxr
