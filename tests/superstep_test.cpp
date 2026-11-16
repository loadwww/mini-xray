// T5.4 验证台：超步归并（纯数据，不联网）
//
// 跑法：./build/superstep_test.exe
// 期望：全部 [ ok ]，末行 "N passed, 0 failed"，退出码 0。

#include "chain/superstep.h"

#include <iostream>
#include <string>
#include <vector>

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

// ── 造事件的小工具（与 link_test 同款，resp 多了 tool_calls 支持）──

static Message sys() { return Message{"system", "You are a helpful agent.", "", ""}; }
static Message user(const std::string &s) { return Message{"user", s, "", ""}; }
static Message assistant(const std::string &s) { return Message{"assistant", s, "", ""}; }
static Message tool(const std::string &id, const std::string &name, const std::string &s)
{
    return Message{"tool", s, id, name};
}

static nlohmann::json reqEvent(const std::string &hop, long long seq, long long ts,
                               const std::vector<Message> &msgs)
{
    nlohmann::json messages = nlohmann::json::array();
    for (const auto &m : msgs) {
        messages.push_back({{"role", m.role}, {"content", m.content},
                            {"tool_call_id", m.toolCallId}, {"name", m.name}});
    }
    nlohmann::json body = {{"model", "gpt-4o-mini"}, {"messages", messages}};
    return {{"type", "req"}, {"hop", hop}, {"seq", seq}, {"ts_ms", ts},
            {"method", "POST"}, {"path", "/v1/chat/completions"}, {"body", body.dump()}};
}

static nlohmann::json respEvent(const std::string &hop, long long seq, long long ts,
                                const std::string &text,
                                const std::vector<ToolCall> &calls = {})
{
    nlohmann::json message = {{"content", text}};
    if (!calls.empty()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &c : calls)
            arr.push_back({{"id", c.id}, {"type", "function"},
                           {"function", {{"name", c.name}, {"arguments", c.argsJson}}}});
        message["tool_calls"] = arr;
    }
    nlohmann::json body = {{"choices", {{{"message", message},
                                          {"finish_reason", calls.empty() ? "stop" : "tool_calls"}}}},
                           {"usage", {{"prompt_tokens", 10}, {"completion_tokens", 5},
                                      {"total_tokens", 15}}}};
    return {{"type", "resp"}, {"hop", hop}, {"seq", seq}, {"ts_ms", ts},
            {"status", 200}, {"body", body.dump()}};
}

int main()
{
    // ═══ 1. classifyToolError：失败分级 ═════════════════════════════

    check(classifyToolError("{\"result\":\"18 degrees\"}", false) == ErrorKind::none,
          "分级：正常结果 → none");
    check(classifyToolError("{\"error\":{\"message\":\"boom\"}}", false) == ErrorKind::confirmed,
          "分级：结果带 error 字段 → confirmed");
    check(classifyToolError("whatever", true) == ErrorKind::confirmed,
          "分级：协议 is_error=true → confirmed（正文不是 JSON 也算）");
    check(classifyToolError("the search failed badly", false) == ErrorKind::inferred,
          "分级：文本命中 failed → inferred");
    check(classifyToolError("tool threw an Exception: timeout", false) == ErrorKind::inferred,
          "分级：大写 Exception 小写化后命中 → inferred");
    check(classifyToolError("user not found in db", false) == ErrorKind::inferred,
          "分级：not found → inferred");
    check(classifyToolError("", false) == ErrorKind::none,
          "分级：空结果 → none（什么证据都没有）");
    check(classifyToolError("{\"ok\":true,\"v\":1}", false) == ErrorKind::none,
          "分级：JSON 正常无失败迹象 → none");
    check(classifyToolError("{\"is_error\":false}", false) == ErrorKind::inferred,
          "分级：键名 is_error 撞词表 → inferred（已知误报，这一级就是为此存在）");

    // ═══ 2. toolsFromDelta：按 tool_call_id 回填 ═══════════════════

    // 3 个并行工具，结果在 messages 里乱序出现
    std::vector<ToolCall> calls3 = {
        {"call-1", "search", "{\"q\":\"weather\"}"},
        {"call-2", "calc", "{\"e\":\"1+1\"}"},
        {"call-3", "read", "{\"path\":\"a.txt\"}"},
    };
    std::vector<Message> msgs3 = {
        sys(), user("task"),
        assistant("calling tools"),
        tool("call-3", "read", "file was not found"),
        tool("call-1", "search", "{\"is_error\":true,\"detail\":\"boom\"}"),
        tool("call-2", "calc", "2"),
    };
    auto t3 = toolsFromDelta(calls3, msgs3, 1180, 120);

    check(t3.size() == 3, "回填：3 个意图都产出 ToolExec（哪怕有的没结果）");
    check(t3[0].toolCallId == "call-1" && t3[1].toolCallId == "call-2" && t3[2].toolCallId == "call-3",
          "回填：顺序跟 prevCalls 一致（不是 messages 里的出现顺序）");
    check(t3[0].resultJson == "{\"is_error\":true,\"detail\":\"boom\"}"
          && t3[1].resultJson == "2"
          && t3[2].resultJson == "file was not found",
          "回填：按 id 对上乱序的结果");
    check(t3[0].name == "search" && t3[0].argsJson == "{\"q\":\"weather\"}",
          "回填：意图三件套（name/args）从 ToolCall 带过来");
    check(t3[0].startMs == 1180 && t3[0].durationMs == 120 && t3[0].durationInferred,
          "回填：共享并行区间 1180→1300，耗时标注为推断");

    // 分级经由 toolsFromDelta 传导
    check(t3[0].error == ErrorKind::confirmed, "回填：is_error:true → confirmed");
    check(t3[1].error == ErrorKind::none, "回填：纯文本 2 → none");
    check(t3[2].error == ErrorKind::inferred, "回填：not found 文本 → inferred");

    // 缺结果：意图存在，messages 里没有对应 tool 消息
    auto tMiss = toolsFromDelta({{"call-9", "search", "{}"}}, msgs3, 100, 50);
    check(tMiss.size() == 1 && tMiss[0].resultJson == "" && tMiss[0].durationMs == 50,
          "回填：缺结果不跳过，resultJson 留空、区间照记");

    // ═══ 3. mergeSupersteps：端到端装配 ════════════════════════════

    // 主 fixture：
    //   run A：h-1 响应发起 3 个并行工具 → h-2 请求带回 3 个结果（乱序）
    //   run B：h-3 响应发起工具但**没有下一步**（缺结果）
    //   run C：h-4 普通回答，无工具
    const std::vector<Message> mA1 = {sys(), user("task A")};
    const std::vector<Message> mA2 = {sys(), user("task A"), assistant("calling tools"),
                                      tool("call-3", "read", "file was not found"),
                                      tool("call-1", "search", "{\"is_error\":true,\"detail\":\"boom\"}"),
                                      tool("call-2", "calc", "2")};
    const std::vector<Message> mB1 = {sys(), user("task B")};
    const std::vector<Message> mC1 = {sys(), user("task C")};

    std::vector<nlohmann::json> events = {
        reqEvent("h-1", 1, 1000, mA1), respEvent("h-1", 2, 1180, "", calls3),
        reqEvent("h-2", 3, 1300, mA2), respEvent("h-2", 4, 1400, "18 degrees and 2"),
        reqEvent("h-3", 5, 1500, mB1), respEvent("h-3", 6, 1600, "",
                                                 {{"call-9", "search", "{\"q\":\"x\"}"}}),
        reqEvent("h-4", 7, 1700, mC1), respEvent("h-4", 8, 1750, "plain answer"),
    };

    auto paired = pairHops(events);
    auto runs = buildRuns(paired.hops);
    mergeSupersteps(runs, paired.hops);

    check(runs.size() == 3, "装配：3 个 run（A 两步 / B 一步缺结果 / C 一步）",
          "runs=" + std::to_string(runs.size()));
    check(runs[0].steps.size() == 2, "装配：run A 仍是两步（merge 不动步数）");

    // run A 第 0 步：3 个并行工具
    const auto &sa0 = runs[0].steps[0];
    check(sa0.tools.size() == 3, "装配：run A 步 0 挂上 3 个 ToolExec");
    bool durOk = sa0.tools.size() == 3;
    for (const auto &t : sa0.tools)
        durOk = durOk && t.durationMs == 120 && t.startMs == 1180;
    check(durOk, "装配：并行 3 工具 → 每个耗时=区间 120，不是相加 360");
    check(sa0.endMs == 1300, "装配：工具结果回来（1300）这步才算完，endMs 从 1180 延后",
          "endMs=" + std::to_string(sa0.endMs));
    check(sa0.tools[0].error == ErrorKind::confirmed
          && sa0.tools[1].error == ErrorKind::none
          && sa0.tools[2].error == ErrorKind::inferred,
          "装配：confirmed / none / inferred 三级在端到端里各就各位");

    // run A 第 1 步：普通回答，无工具
    check(runs[0].steps[1].tools.empty() && runs[0].steps[1].endMs == 1400,
          "装配：run A 步 1 无工具，endMs 不动（1400）");

    // run B：最后一步带 toolCalls，没有下一请求
    const auto &sb0 = runs[1].steps[0];
    check(sb0.tools.size() == 1 && sb0.tools[0].resultJson == "" && sb0.tools[0].durationMs == 0,
          "装配：链尾发起工具但缺结果 → resultJson 空、区间 0");
    check(sb0.endMs == 1600, "装配：没有下一请求时不改 endMs（保持响应时间 1600）",
          "endMs=" + std::to_string(sb0.endMs));

    // run C：没发起工具，merge 后原样
    const auto &sc0 = runs[2].steps[0];
    check(sc0.tools.empty() && sc0.endMs == 1750 && sc0.outputText == "plain answer",
          "装配：无工具的步保持原样");

    // 负时钟：下一请求比上一响应还早（乱序流量），区间不许为负
    const std::vector<Message> mD2 = {sys(), user("skew"), assistant("go"),
                                      tool("call-s", "search", "late")};
    std::vector<nlohmann::json> evSkew = {
        reqEvent("h-5", 9, 1900, {sys(), user("skew")}),
        respEvent("h-5", 10, 2000, "", {{"call-s", "search", "{}"}}),
        reqEvent("h-6", 11, 1990, mD2),
        respEvent("h-6", 12, 2050, "done"),
    };
    auto pSkew = pairHops(evSkew);
    auto rSkew = buildRuns(pSkew.hops);
    mergeSupersteps(rSkew, pSkew.hops);
    check(rSkew[0].steps[0].tools.size() == 1
          && rSkew[0].steps[0].tools[0].durationMs == 0,
          "装配：负区间钳成 0（不出现负耗时）");

    // ═══ 4. 装配后整图 JSON 往返 ═══════════════════════════════════
    nlohmann::json j = runs;
    auto back = j.get<std::vector<Run>>();
    check(back == runs, "装配：Run→JSON→Run 往返一致（ToolExec 序列化无丢字段）");

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
