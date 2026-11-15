#pragma once

#include <string>

namespace mxr {

// ═══════════════════════════════════════════════════════════════════
//  SseAccumulator —— 把逐块到达的 SSE 字节流还原成"一次回答"
// ═══════════════════════════════════════════════════════════════════
//
// 【它解决的是哪个矛盾】
//
//   下游（httplib / 事件文件）看到的：一串 data: {...}、[DONE]、可能被
//   TCP 任意切碎的字节 —— 这是"传输形状"，不是"回答"。
//
//   M5 重建链路、M6 出报告要的是：一句完整的话、token 用量、为什么停。
//   这中间的翻译工作就是本类：把"流的形状"还原成"语义"。
//
// 【边界：它认识什么、不认识什么】
//
//   认识：SSE 的行规则（data: 前缀、空行边界、: 注释）、[DONE] 约定、
//         OpenAI chunk 的 JSON 形状（delta.content / finish_reason / usage）
//   不认识：HTTP（不碰状态码和 header）、socket（不收字节，feed 进来的
//         已经是解码后的 body）、chunked（T4.1 那层在它之前就拆完了）
//
//   为什么用"累积器"而不是"回调"：T4.4 的 replay 要做的事恰好是
//   "把 events.jsonl 里的原始 data 重新喂一遍、看看能不能得到同一句话"——
//   累积器天然可重入（建一个新的、把数据再喂一遍），回调式反而要搭管道。
//
// 【状态为什么是两个缓冲，不是一个】
//
//   lineBuf_  —— "半行"缓冲。feed 进来的字节可能把一行切成两半
//                （"data: {\"ch" + "oices\":...}"），没见到 \n 之前
//                谁也不能确定这是一整行，只能先攒着。
//   dataAcc_  —— "半个事件"缓冲。SSE 的事件边界是**空行**，不是换行；
//                一个事件可以有多行 data（规范要求用 \n 拼接）。
//                见到空行之前，data 行只能先累积。
//
//   两级缓冲的关系：字节 --(\n 切行)--> 行 --(空行分发)--> 事件。
//   每一级都必须"见到边界才交出去"，这就是流式解析的全部秘密。
//
// 【生命周期】
//
//   一次流式响应 = 一个累积器。T4.4 里它会出现在两个地方：
//     在线：server 转发时顺便喂它（攒 text/usage 给 sse_end 事件）
//     离线：replay 命令重建时再喂一遍（和 sse_end 对账）

// token 用量。OpenAI 在流里最后一块（带 stream_options.include_usage 时
// 或有的服务端默认）会附一个顶层 usage 对象，不在 choices 里。
struct SseUsage {
    long long promptTokens = 0;
    long long completionTokens = 0;
    long long totalTokens = 0;
};

class SseAccumulator {
public:
    // ── 输入 ──────────────────────────────────────────────────────

    // 喂一段原始 SSE 字节。可以一次一整块，也可以一次 1 字节；
    // 可以带半行、可以带多个完整事件 —— 都由内部缓冲消化。
    void feed(const std::string &bytes);

    // 流结束时调用：把可能残留的"没有换行符的半行"当最后一行处理。
    // 之后不应再 feed。
    void finish();

    // ── 查询 ──────────────────────────────────────────────────────

    bool done() const;

    // 所有 delta.content 拼起来的完整文本（"tok0 tok1 ..."）
    const std::string &text() const;

    // token 用量。hasUsage() 为 false 时三个字段都是 0（流里没给）。
    bool hasUsage() const;
    const SseUsage &usage() const;

    // 最后一次见到的非空 finish_reason（"stop" / "length" / "tool_calls"…）
    const std::string &finishReason() const;

    // JSON 解析失败的事件数（容错的证据：坏了几个，但流程没崩）
    int badDataCount() const;

    // 成功解析并采纳的 data 事件数（不含 [DONE]，不含坏块）
    int eventCount() const;

private:
    // 处理一整行（已剥掉行尾 \r）。分类规则见 .cpp。
    void processLine(const std::string &line);

    // 空行触发：把 dataAcc_ 里攒的事件交出去解析。
    void dispatch(const std::string &data);

    std::string lineBuf_;      // 半行缓冲（没见到 \n 的部分）
    std::string dataAcc_;      // 半事件缓冲（没见到空行的 data 累积）

    std::string text_;
    std::string finishReason_;
    SseUsage usage_;
    bool hasUsage_ = false;

    bool done_ = false;
    int badDataCount_ = 0;
    int eventCount_ = 0;
};

} // namespace mxr
