// T5.1 验证台：执行图数据模型的 JSON 往返一致性（纯数据，不联网、不写文件）
//
// 跑法：./build/model_test.exe
// 期望：全部 [ ok ]，末行 "N passed, 0 failed"，退出码 0。
//
// 验收两条：
//   1. from_json(to_json(x)) == x（含嵌套 vector）
//   2. operator== 覆盖每个字段 —— 逐个字段变异，漏比任何一个都会被抓到

#include "model/model.h"

#include <functional>
#include <iostream>
#include <string>
#include <utility>
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

// 造一份三层都填满的样本
static Run makeSample()
{
    ToolExec t;
    t.toolCallId = "call-7";
    t.name = "search";
    t.argsJson = "{\"q\":\"weather\"}";
    t.resultJson = "{\"temp\":18}";
    t.startMs = 1200;
    t.durationMs = 340;
    t.error = ErrorKind::confirmed;
    t.durationInferred = true;

    Step s;
    s.index = 2;
    s.startMs = 1000;
    s.endMs = 2000;
    s.model = "gpt-4o-mini";
    s.requestFingerprint = "fp-a1b2c3";
    s.tools = {t};
    s.outputText = "今天 18 度";
    s.usage = Usage{10, 5, 15, true};
    s.complete = true;

    Step s2;
    s2.index = 3;
    s2.outputText = "bye";
    s2.complete = false;

    Run r;
    r.id = "run-20261115-001";
    r.steps = {s, s2};
    r.startMs = 1000;
    r.endMs = 3000;
    return r;
}

int main()
{
    const Run sample = makeSample();

    // ── 1. 完整往返 ──────────────────────────────────────────────
    {
        nlohmann::json j = sample;             // 触发 ADL to_json
        const Run back = j.get<Run>();         // 触发 ADL from_json
        check(back == sample, "往返：嵌套 Run 完全一致");
        check(back.steps.size() == 2 && back.steps[0].tools.size() == 1,
              "往返：vector<Step> / vector<ToolExec> 都回来了");
    }

    // ── 2. JSON 形状：字段名、枚举是字符串 ────────────────────────
    {
        nlohmann::json j = sample;
        check(j.contains("id") && j.contains("steps") && j.contains("start_ms")
                  && j.contains("end_ms"),
              "形状：Run 的四个键都在");
        const auto &s = j["steps"][0];
        check(s.contains("usage") && s["usage"].contains("observed"),
              "形状：Step 里嵌了 usage");
        check(s["tools"][0]["error"] == "confirmed",
              "形状：error 写成字符串而不是数字",
              j["steps"][0]["tools"][0].value("error", std::string("")));
        check(s["usage"]["observed"].is_boolean(),
              "形状：observed 是 bool");
        check(s["complete"].is_boolean() && s["index"].is_number(),
              "形状：complete / index 类型正确");
    }

    // ── 3. 缺字段要回落默认值，不能抛 ─────────────────────────────
    {
        nlohmann::json j = {{"name", "search"}, {"error", "inferred"}};
        const ToolExec t = j.get<ToolExec>();
        check(t.name == "search" && t.error == ErrorKind::inferred
                  && t.toolCallId.empty() && t.durationMs == 0
                  && t.durationInferred == false,
              "鲁棒：只有两个键也能读回，其余取默认值");
    }

    // ── 4. 三种 ErrorKind 各自往返 ────────────────────────────────
    {
        bool allOk = true;
        for (ErrorKind k : {ErrorKind::none, ErrorKind::inferred, ErrorKind::confirmed}) {
            ToolExec t;
            t.error = k;
            nlohmann::json j = t;
            allOk = allOk && (j.get<ToolExec>().error == k);
        }
        check(allOk, "枚举：none / inferred / confirmed 往返不串味");
        nlohmann::json unknown = {{"error", "???"}};
        check(unknown.get<ToolExec>().error == ErrorKind::none,
              "枚举：未知字符串回落 none");
    }

    // ── 5. observed 区分"未观测"和"真的是 0" ──────────────────────
    {
        const Usage unobserved{0, 0, 0, false};
        const Usage zero{0, 0, 0, true};
        check(!(unobserved == zero), "usage：未观测 != 真的是 0");
        nlohmann::json j = unobserved;
        check(j.get<Usage>() == unobserved, "usage：observed=false 往返不丢");
    }

    // ── 6. 空 Run 也要能往返 ──────────────────────────────────────
    {
        const Run empty;
        check(nlohmann::json(empty).get<Run>() == empty, "空 Run：往返一致");
    }

    // ── 7. operator== 字段覆盖：逐个字段改一下，必须判为不等 ──────
    {
        using Mut = std::function<void(Run &)>;
        const std::vector<std::pair<std::string, Mut>> mutations = {
            {"Run.id", [](Run &r) { r.id = "other"; }},
            {"Run.steps", [](Run &r) { r.steps.clear(); }},
            {"Run.startMs", [](Run &r) { r.startMs += 1; }},
            {"Run.endMs", [](Run &r) { r.endMs += 1; }},
            {"Step.index", [](Run &r) { r.steps[0].index += 1; }},
            {"Step.startMs", [](Run &r) { r.steps[0].startMs += 1; }},
            {"Step.endMs", [](Run &r) { r.steps[0].endMs += 1; }},
            {"Step.model", [](Run &r) { r.steps[0].model = "x"; }},
            {"Step.requestFingerprint", [](Run &r) { r.steps[0].requestFingerprint = "x"; }},
            {"Step.tools", [](Run &r) { r.steps[0].tools.clear(); }},
            {"Step.outputText", [](Run &r) { r.steps[0].outputText = "x"; }},
            {"Step.usage", [](Run &r) { r.steps[0].usage.totalTokens += 1; }},
            {"Step.complete", [](Run &r) { r.steps[0].complete = !r.steps[0].complete; }},
            {"ToolExec.toolCallId", [](Run &r) { r.steps[0].tools[0].toolCallId = "x"; }},
            {"ToolExec.name", [](Run &r) { r.steps[0].tools[0].name = "x"; }},
            {"ToolExec.argsJson", [](Run &r) { r.steps[0].tools[0].argsJson = "x"; }},
            {"ToolExec.resultJson", [](Run &r) { r.steps[0].tools[0].resultJson = "x"; }},
            {"ToolExec.startMs", [](Run &r) { r.steps[0].tools[0].startMs += 1; }},
            {"ToolExec.durationMs", [](Run &r) { r.steps[0].tools[0].durationMs += 1; }},
            {"ToolExec.error", [](Run &r) { r.steps[0].tools[0].error = ErrorKind::none; }},
            {"ToolExec.durationInferred", [](Run &r) { r.steps[0].tools[0].durationInferred = false; }},
            {"Usage.promptTokens", [](Run &r) { r.steps[0].usage.promptTokens += 1; }},
            {"Usage.completionTokens", [](Run &r) { r.steps[0].usage.completionTokens += 1; }},
            {"Usage.totalTokens", [](Run &r) { r.steps[0].usage.totalTokens += 1; }},
            {"Usage.observed", [](Run &r) { r.steps[0].usage.observed = false; }},
        };
        int missed = 0;
        for (const auto &kv : mutations) {
            Run changed = sample;
            kv.second(changed);
            if (changed == sample) {
                ++missed;
                std::cout << "       << 漏比字段: " << kv.first << "\n";
            }
        }
        check(missed == 0, "operator==：25 个字段逐个变异都被判为不等",
              "漏比 " + std::to_string(missed) + " 个");
    }

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
