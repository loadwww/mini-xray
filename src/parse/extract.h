#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "model/model.h"

namespace mxr {

// ═══════════════════════════════════════════════════════════════════
//  协议字段提取：把三种 JSON 形状翻译成同一种结论
// ═══════════════════════════════════════════════════════════════════
//
// 【这层是干嘛的】
//
//   M4 落盘的是原始字节/文本，T5.1 定义的是"结论"的形状。中间缺一步：
//   把 OpenAI 协议的字段位置认出来。本层做这一步，三种输入共用一套输出：
//
//     req body      ──> parseRequest   ──> RequestInfo
//     非流式 resp    ──> parseResponse  ──> ResponseInfo
//     sse_end 事件   ──> parseSseEnd    ──> ResponseInfo   ← 同一个类型！
//
//   为什么流式和非流式必须产出**同一个** ResponseInfo：
//     T5.3 建链不该关心这次请求是不是 stream，否则每条判断都要写两遍。
//     验收里"流式/非流式关键字段一致"就是靠这个结构保证的。
//
// 【边界】
//
//   只读 JSON，不碰 HTTP、不碰文件、不碰 events.jsonl 的读取、不抛异常。
//   坏数据（不是 JSON、字段类型不对）一律取默认值并计一次 parseFailed。
//
// 【为什么 sse_end 不用自己累积 delta】
//
//   M4 的 SseAccumulator 在在线路径已经把 delta.content 拼成 text、
//   把最后一块的 usage 提取出来了；sse_end 就是**累积后的结论**。
//   本层消费结论，不重复实现状态机。tool_calls 的流式 delta 累积留到 T5.4
//   （mock 上游不产工具调用），所以 parseSseEnd 的 toolCalls 暂时为空。

// messages[] 里的一条。T5.3 靠它算前缀哈希，T5.4 靠 toolCallId 回填工具结果。
struct Message {
    std::string role;
    std::string content;
    std::string toolCallId;   // role:"tool" 时对应哪次调用
    std::string name;         // role:"tool" 时的工具名
};

// 模型"发起"的一次工具调用。和 model::ToolExec 不是一回事：
// ToolCall 是意图（id / name / 参数），ToolExec 是执行结果（回填后才有耗时与失败等级）。
struct ToolCall {
    std::string id;
    std::string name;
    std::string argsJson;     // 原始参数文本，形状不由本层规定
};

struct RequestInfo {
    std::string model;
    std::vector<Message> messages;
    std::vector<std::string> toolNames;   // tools[].function.name
    bool stream = false;
    int parseFailed = 0;
};

struct ResponseInfo {
    std::string outputText;
    std::vector<ToolCall> toolCalls;
    std::string finishReason;
    Usage usage;                  // 复用 T5.1：observed 区分"未观测"与"真的是 0"
    int parseFailed = 0;
};

RequestInfo parseRequest(const std::string &body);
ResponseInfo parseResponse(const std::string &body);
ResponseInfo parseSseEnd(const nlohmann::json &ev);

} // namespace mxr
