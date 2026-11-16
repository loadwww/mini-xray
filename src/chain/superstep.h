#pragma once

#include <string>
#include <vector>

#include "model/model.h"
#include "chain/link.h"

namespace mxr {

// ═══════════════════════════════════════════════════════════════════
//  超步归并：给每轮模型调用挂上"它发起的工具到底执行成了什么样"
// ═══════════════════════════════════════════════════════════════════
//
// 【这层是干嘛的】
//
//   T5.3 串出来的 Run/Step 只有"模型说了什么"（outputText），
//   Step.tools 是空的。但 agent 循环里最有信息量的恰恰是工具：
//     assistant 回复带 tool_calls → 客户端执行 → 把结果追加进 messages
//   这些信息全在流量里，只是拆在两个 hop 之间：
//     · 发起方：上一 hop 的 ResponseInfo.toolCalls（意图：id/name/参数）
//     · 结果方：下一 hop 的 RequestInfo.messages 里 role=="tool" 的消息
//   本层把两半对上，拼出完整的 ToolExec（执行：参数 + 结果 + 耗时 + 失败等级）。
//
// 【为什么"超步"】
//
//   一次模型调用可能**并行**发起 N 个工具调用（一个 tool_calls 数组）。
//   从代理流量只能观察到一个区间：上一响应结束 → 下一请求开始。
//   区间是共享的，拆不出单个工具的精确耗时——所以按并行假设处理：
//   每个工具 durationMs = 整个区间，而不是把区间按工具数均摊或相加。
//
// 【边界】
//
//   只读 Hop 里已提取好的结构（toolCalls / messages），不碰 HTTP、
//   不重算前缀哈希（Step.requestFingerprint 是 T5.3 存好的"回程票"）。
//
// 【失败为什么分两级】
//
//   confirmed：协议层明确说失败（结果 JSON 带 error 字段 / is_error:true），
//              这是上游给的结论，可信。
//   inferred ：只是结果**文本**里像失败（"error"/"failed"/…），
//              工具可能把 error 字符串当正常返回值——可能误报，所以单独一级。
//   UI/M6 把两级染不同颜色，人看到 "~" 和 inferred 就知道别全信。

// 判定一次工具执行的失败等级。
// 判定顺序（重要）：protocolIsError → 结果 JSON 的 error 字段 → 文本词表。
// protocolIsError 由调用方从结果 JSON 的 is_error 字段读出来传入。
ErrorKind classifyToolError(const std::string &resultJson, bool protocolIsError);

// 把上一响应发起的 toolCalls 变成 ToolExec，结果从 curMsgs 里
// 按 tool_call_id 匹配 role=="tool" 的消息回填。
// windowStartMs/windowMs 是共享的并行区间；找不到结果 → resultJson 空。
std::vector<ToolExec> toolsFromDelta(const std::vector<ToolCall> &prevCalls,
                                     const std::vector<Message> &curMsgs,
                                     long long windowStartMs,
                                     long long windowMs);

// 顶层归并：给 runs 里每个 Step 挂上 tools（就地修改，不返回新值）。
// Step ↔ Hop 靠 requestFingerprint 对回；相邻两 hop 之间是工具区间；
// 有工具的步 endMs 延到下一请求开始（结果回来才算这步完）。
void mergeSupersteps(std::vector<Run> &runs, const std::vector<Hop> &hops);

} // namespace mxr
