#include "store/event.h"

namespace mxr {

// 事件 → 一行 JSON；流式事件按 type 追加各自的条件字段。
nlohmann::json toJson(const Event& ev)
{
    nlohmann::json j;
    j["type"] = ev.type;
    j["hop"] = ev.hop;
    j["seq"] = ev.seq;
    j["ts_ms"] = ev.tsMs;
    j["method"] = ev.method;
    j["path"] = ev.path;
    j["upstream"] = ev.upstream;
    j["status"] = ev.status;
    j["latency_ms"] = ev.latencyMs;
    j["headers"] = ev.headers;
    j["body"] = ev.body;

    if (ev.type == "sse_chunk") {
        j["chunk_index"] = ev.chunkIndex;
    } else if (ev.type == "sse_end") {
        j["text"] = ev.text;
        j["usage"] = {{"prompt_tokens", ev.promptTokens},
                      {"completion_tokens", ev.completionTokens},
                      {"total_tokens", ev.totalTokens}};
        j["duration_ms"] = ev.durationMs;
        j["bad_data"] = ev.badDataCount;
        j["finish_reason"] = ev.finishReason;
    }
    
    return j;
}

} // namespace mxr
