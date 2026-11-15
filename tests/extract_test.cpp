// T5.2 验证台：协议字段提取（纯 JSON，不联网、不写文件）
//
// 跑法：./build/extract_test.exe
// 期望：全部 [ ok ]，末行 "N passed, 0 failed"，退出码 0。

#include "parse/extract.h"
#include "store/event.h"

#include <iostream>
#include <string>

using namespace mxr;

static int g_pass = 0;
static int g_fail = 0;

static void check(bool ok, const std::string &what, const std::string &detail = "")
{
    if (ok) {
        ++g_pass;
        std::cout << "[ ok ] " << what << "\n";
    } else {
        ++g_fail;
        std::cout << "[FAIL] " << what;
        if (!detail.empty()) std::cout << "   << " << detail;
        std::cout << "\n";
    }
}

static const std::string kText = "hi there";
static const Usage kUsage{10, 5, 15, true};

static std::string requestBody()
{
    return R"({"model":"gpt-4o-mini","stream":true,
               "messages":[{"role":"system","content":"you are helpful"},
                           {"role":"user","content":"weather?"}],
               "tools":[{"type":"function","function":{"name":"search"}},
                        {"type":"function","function":{"name":"calc"}}]})";
}

static std::string responseBody()
{
    return R"({"choices":[{"message":{"content":"hi there","tool_calls":[
                 {"id":"call-1","type":"function",
                  "function":{"name":"search","arguments":"{\"q\":\"x\"}"}}]},
                 "finish_reason":"stop"}],
             "usage":{"prompt_tokens":10,"completion_tokens":5,"total_tokens":15}})";
}

// 同一份 mock 数据对应的流式结论（M4 的 sse_end 形状）
static nlohmann::json sseEndEvent()
{
    return {{"type", "sse_end"}, {"hop", "h-1"}, {"text", kText},
            {"usage", {{"prompt_tokens", 10}, {"completion_tokens", 5}, {"total_tokens", 15}}},
            {"finish_reason", "stop"}, {"bad_data", 0}};
}

int main()
{
    // ── 1. 请求提取 ────────────────────────────────────────────────
    {
        const auto r = parseRequest(requestBody());
        check(r.model == "gpt-4o-mini" && r.stream, "req：model / stream");
        check(r.messages.size() == 2 && r.messages[0].role == "system"
                  && r.messages[1].content == "weather?",
              "req：messages 逐条取出", std::to_string(r.messages.size()));
        check(r.toolNames.size() == 2 && r.toolNames[0] == "search"
                  && r.toolNames[1] == "calc",
              "req：tools[].function.name");
        check(r.parseFailed == 0, "req：正常数据零失败", std::to_string(r.parseFailed));
    }

    // ── 1b. tool 消息：键名是 snake_case 的 tool_call_id ───────────
    {
        const auto r = parseRequest(R"({"messages":[
            {"role":"tool","tool_call_id":"call-1","name":"search","content":"18度"}]})");
        check(r.messages.size() == 1 && r.messages[0].toolCallId == "call-1"
                  && r.messages[0].name == "search" && r.messages[0].content == "18度",
              "req：tool 消息的 tool_call_id / name / content",
              r.messages.empty() ? "no messages" : r.messages[0].toolCallId);
        check(r.parseFailed == 0, "req：tool 消息不该计 parseFailed");
    }

    // ── 2. 非流式响应提取 ──────────────────────────────────────────
    {
        const auto r = parseResponse(responseBody());
        check(r.outputText == kText, "resp：content 取自 choices[0].message", r.outputText);
        check(r.finishReason == "stop", "resp：finish_reason 取自 choices[0]", r.finishReason);
        check(r.toolCalls.size() == 1 && r.toolCalls[0].id == "call-1"
                  && r.toolCalls[0].name == "search"
                  && r.toolCalls[0].argsJson == "{\"q\":\"x\"}",
              "resp：tool_calls 取自 message 内", std::to_string(r.toolCalls.size()));
        check(r.usage == kUsage, "resp：顶层 usage 且 observed=true");
        check(r.parseFailed == 0, "resp：正常数据零失败");
    }

    // ── 3. 流式结论提取 ────────────────────────────────────────────
    {
        const auto r = parseSseEnd(sseEndEvent());
        check(r.outputText == kText, "sse_end：text 直接当成累积文本", r.outputText);
        check(r.finishReason == "stop", "sse_end：finish_reason", r.finishReason);
        check(r.usage == kUsage, "sse_end：usage 且 observed=true");
        check(r.toolCalls.empty(), "sse_end：流式 tool_calls 留到 T5.4");
        check(r.parseFailed == 0, "sse_end：正常数据零失败");
    }

    // ── 4. 核心验收：流式 / 非流式关键字段一致 ─────────────────────
    {
        const auto a = parseResponse(responseBody());
        const auto b = parseSseEnd(sseEndEvent());
        check(a.outputText == b.outputText && a.finishReason == b.finishReason
                  && a.usage == b.usage,
              "一致性：同一份 mock 数据两种路径结论相同");
    }

    // ── 5. usage 的 observed：未观测 vs 真的是 0 ───────────────────
    {
        const auto noUsage = parseResponse(R"({"choices":[{"message":{"content":"x"}}]})");
        check(!noUsage.usage.observed && noUsage.usage.totalTokens == 0,
              "usage：缺键 → 未观测");

        const auto zero = parseResponse(
            R"({"choices":[],"usage":{"prompt_tokens":0,"completion_tokens":0,"total_tokens":0}})");
        check(zero.usage.observed && zero.usage.totalTokens == 0,
              "usage：给了全 0 → 真的是 0（observed=true）");
        check(!(noUsage.usage == zero.usage), "usage：两者不相等");

        const auto sseNoUsage = parseSseEnd({{"type", "sse_end"}, {"text", "x"}});
        check(!sseNoUsage.usage.observed, "sse_end：缺 usage → 未观测");
    }

    // ── 6. 鲁棒性：坏数据不崩，只计数 ──────────────────────────────
    {
        const auto a = parseRequest("not json at all");
        check(a.model.empty() && a.parseFailed == 1, "鲁棒：req 非 JSON 不崩");
        const auto b = parseRequest(R"({"model":42,"stream":"yes"})");
        check(b.model.empty() && b.parseFailed >= 2 && !b.stream,
              "鲁棒：req 类型不符计数", std::to_string(b.parseFailed));
        const auto c = parseResponse(R"({"choices":[{"message":{"content":123}}]})");
        check(c.outputText.empty() && c.parseFailed >= 1,
              "鲁棒：resp 类型不符计数", std::to_string(c.parseFailed));
        const auto d = parseResponse(R"({"choices":"oops"})");
        check(d.outputText.empty() && d.parseFailed >= 1, "鲁棒：choices 不是数组");
        const auto e = parseResponse("");
        check(e.parseFailed == 1 && !e.usage.observed, "鲁棒：空 body 不崩");
    }

    // ── 7. Event 扩展：sse_end 真的带上了 finish_reason ────────────
    {
        Event ev;
        ev.type = "sse_end";
        ev.text = kText;
        ev.finishReason = "stop";
        ev.promptTokens = 10;
        ev.completionTokens = 5;
        ev.totalTokens = 15;
        const nlohmann::json j = toJson(ev);
        check(j.value("finish_reason", "") == "stop", "Event：sse_end 带 finish_reason 键");
        const auto back = parseSseEnd(j);
        check(back.outputText == kText && back.finishReason == "stop" && back.usage.observed,
              "Event → json → parseSseEnd 全链路通");
    }

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
