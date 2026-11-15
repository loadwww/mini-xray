// T5.3 验证台：建链（纯数据，不联网；会写 runs/link_fixture.jsonl）
//
// 跑法：./build/link_test.exe
// 期望：全部 [ ok ]，末行 "N passed, 0 failed"，退出码 0。

#include "chain/link.h"

#include <fstream>
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

static Message sys() { return Message{"system", "You are a helpful agent.", "", ""}; }
static Message user(const std::string &s) { return Message{"user", s, "", ""}; }
static Message assistant(const std::string &s) { return Message{"assistant", s, "", ""}; }
static Message tool(const std::string &id, const std::string &s)
{
    return Message{"tool", s, id, "search"};
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
                                const std::string &text)
{
    nlohmann::json body = {{"choices", {{{"message", {{"content", text}}},
                                          {"finish_reason", "stop"}}}},
                           {"usage", {{"prompt_tokens", 10}, {"completion_tokens", 5},
                                      {"total_tokens", 15}}}};
    return {{"type", "resp"}, {"hop", hop}, {"seq", seq}, {"ts_ms", ts},
            {"status", 200}, {"body", body.dump()}};
}

int main()
{
    // 主 fixture：一条 3 步链 + 一个共享 system prompt 的独立任务
    const std::vector<Message> m1 = {sys(), user("task A")};
    const std::vector<Message> m2 = {sys(), user("task A"), assistant("call search"),
                                     tool("call-1", "18度")};
    const std::vector<Message> m3 = {sys(), user("task A"), assistant("call search"),
                                     tool("call-1", "18度"), user("and tomorrow?")};
    const std::vector<Message> m4 = {sys(), user("task B")};

    std::vector<nlohmann::json> events = {
        reqEvent("h-1", 1, 1000, m1), respEvent("h-1", 2, 1050, "thinking"),
        reqEvent("h-2", 3, 1100, m2), respEvent("h-2", 4, 1180, "18度"),
        reqEvent("h-3", 5, 1200, m3), respEvent("h-3", 6, 1300, "tomorrow 20度"),
        reqEvent("h-4", 7, 1400, m4), respEvent("h-4", 8, 1450, "ok"),
    };

    // ── 1. readEvents：坏行跳过 ────────────────────────────────────
    {
        std::ofstream out("runs/link_fixture.jsonl", std::ios::trunc);
        for (const auto &e : events) out << e.dump() << "\n";
        out << "这不是 JSON\n\n";
        out.close(); // 先落盘，否则下面的 readEvents 读到的是没 flush 的空文件
        const auto got = readEvents("runs/link_fixture.jsonl");
        check(got.size() == events.size(), "readEvents：坏行被跳过",
              std::to_string(got.size()));
        check(readEvents("runs/no_such_file.jsonl").empty(), "readEvents：缺文件返回空");
    }

    // ── 2. pairHops：配对 + 排序 + 孤儿计数 ────────────────────────
    PairResult paired;
    {
        auto withOrphan = events;
        withOrphan.push_back(respEvent("h-99", 9, 1500, "orphan"));
        paired = pairHops(withOrphan);
        check(paired.hops.size() == 4, "pairHops：4 条请求-响应对",
              std::to_string(paired.hops.size()));
        check(paired.orphanResponses == 1, "pairHops：孤儿 resp 被计数");
        bool sorted = true;
        for (std::size_t i = 1; i < paired.hops.size(); ++i) {
            if (paired.hops[i - 1].seq > paired.hops[i].seq) sorted = false;
        }
        check(sorted, "pairHops：按 seq 升序");
        check(paired.hops[0].request.messages.size() == 2
                  && paired.hops[0].response.outputText == "thinking"
                  && paired.hops[0].startMs == 1000 && paired.hops[0].endMs == 1050,
              "pairHops：req/resp 字段都到位");
    }

    // ── 3. prefixHashes 与朴素版对账 ───────────────────────────────
    {
        const auto h1 = prefixHashes(m1), h2 = prefixHashes(m2), h3 = prefixHashes(m3);
        check(h1.size() == 2 && h2.size() == 4 && h3.size() == 5,
              "prefixHashes：长度等于 messages 条数");
        check(h1.back() == h2[1] && h2.back() == h3[3],
              "prefixHashes：相同前缀对应下标同值");
        check(h1[1] != h2[3], "prefixHashes：不同消息不同哈希");

        bool consistent = true;
        for (const auto &a : {m1, m2, m3, m4}) {
            const auto ha = prefixHashes(a);
            for (const auto &b : {m1, m2, m3, m4}) {
                const auto hb = prefixHashes(b);
                const bool naive = isStrictPrefix(a, b);
                const bool hashed = !a.empty() && a.size() < b.size()
                                 && hb[a.size() - 1] == ha.back();
                if (naive != hashed) consistent = false;
            }
        }
        check(consistent, "一致性：哈希版与 O(n²) 全量版判定相同");
    }

    // ── 4. buildRuns：3 步链 + 独立任务 = 2 个 run ─────────────────
    std::vector<Run> runs;
    {
        runs = buildRuns(paired.hops);
        check(runs.size() == 2, "建链：2 个 run", std::to_string(runs.size()));
        if (runs.size() == 2) {
            check(runs[0].steps.size() == 3 && runs[1].steps.size() == 1,
                  "建链：步数 3 / 1",
                  std::to_string(runs[0].steps.size()) + " / "
                      + std::to_string(runs[1].steps.size()));
            check(runs[0].steps[0].index == 0 && runs[0].steps[1].index == 1
                      && runs[0].steps[2].index == 2,
                  "建链：Step.index 递增");
            check(runs[0].steps[2].outputText == "tomorrow 20度"
                      && runs[0].steps[2].usage.observed,
                  "建链：Step 带上了响应结论", runs[0].steps[2].outputText);
            check(!runs[0].steps[0].requestFingerprint.empty()
                      && runs[0].steps[0].model == "gpt-4o-mini",
                  "建链：Step 带上了 model 与指纹");
            check(runs[0].startMs == 1000 && runs[0].endMs == 1300,
                  "建链：Run 时间取首尾", std::to_string(runs[0].endMs));
        }
    }

    // ── 5. 稳定 id：同一份事件重复执行结果相同 ─────────────────────
    {
        const auto again = buildRuns(pairHops(readEvents("runs/link_fixture.jsonl")).hops);
        bool same = again.size() == runs.size();
        for (std::size_t i = 0; same && i < runs.size(); ++i) {
            same = again[i].id == runs[i].id;
        }
        check(same, "稳定 id：重放同一份 events.jsonl 得到相同 run id");
        if (runs.size() == 2) {
            check(runs[0].id != runs[1].id, "稳定 id：不同 run 的 id 不同");
        }
    }

    // ── 6. system-only 陷阱：只有 system 相同不能串 ────────────────
    {
        const std::vector<Message> onlySys = {sys()};
        const std::vector<Message> otherTask = {sys(), user("another task")};
        std::vector<nlohmann::json> ev = {
            reqEvent("h-a", 1, 2000, onlySys), respEvent("h-a", 2, 2010, "x"),
            reqEvent("h-b", 3, 2100, otherTask), respEvent("h-b", 4, 2150, "y"),
        };
        const auto r = buildRuns(pairHops(ev).hops);
        check(r.size() == 2, "system-only：共享 system 不串成一条链",
              std::to_string(r.size()));
    }

    // ── 7. 空输入 / 全部无前缀 ─────────────────────────────────────
    {
        check(buildRuns({}).empty(), "空输入：0 个 run");
        std::vector<Hop> independents;
        for (int i = 0; i < 3; ++i) {
            Hop h;
            h.hop = "h-" + std::to_string(i);
            h.seq = i + 1;
            h.request.messages = {sys(), user("task " + std::to_string(i))};
            independents.push_back(h);
        }
        check(buildRuns(independents).size() == 3, "三个互不为前缀的请求 → 3 个 run");
    }

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
