// T6.1 验证台：Mermaid 图输出（纯字符串，不联网、不写文件）
//
// 跑法：./build/mermaid_test.exe            跑断言
//       ./build/mermaid_test.exe --dump     打印 fixture 生成的 flowchart 与 gantt
// 期望：全部 [ ok ]，末行 "N passed, 0 failed"，退出码 0。

#include "graph/mermaid.h"

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

static std::size_t countOf(const std::string &s, const std::string &sub)
{
    std::size_t n = 0, pos = 0;
    while ((pos = s.find(sub, pos)) != std::string::npos) { ++n; pos += sub.size(); }
    return n;
}

// 造一个 2 步 Run：step0 带 3 工具（含 confirmed 失败 + inferred），step1 普通
static Run makeRun()
{
    Run r;
    r.id = "run-1000-abc-0";
    r.startMs = 1000;
    r.endMs = 1400;

    Step s0;
    s0.index = 0;
    s0.startMs = 1000; s0.endMs = 1180;
    s0.model = "gpt-4o-mini";
    s0.usage = Usage{10, 5, 15, true};
    s0.complete = true;
    s0.tools = {
        ToolExec{"c1", "search", "{}", "18度", 1180, 120, ErrorKind::none, true},
        ToolExec{"c2", "calc", "{}", "{\"error\":\"bad\"}", 1180, 120, ErrorKind::confirmed, true},
        ToolExec{"c3", "read \"file\" & <x>", "{}", "not found", 1180, 120, ErrorKind::inferred, false},
    };
    s0.outputText = "result";
    r.steps.push_back(s0);

    Step s1;
    s1.index = 1;
    s1.startMs = 1200; s1.endMs = 1400;
    s1.model = "gpt-4o";
    s1.usage = Usage{0, 0, 0, false};
    s1.complete = false;
    r.steps.push_back(s1);
    return r;
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "--dump")
    {
        Run demo = makeRun();
        std::cout << "===== flowchart (graph.mmd) =====\n"
                  << emitMermaid(demo)
                  << "===== gantt (timeline.mmd) =====\n"
                  << emitTimeline(demo);
        return 0;
    }

    // ═══ 1. 转义 ═════════════════════════════════════════════════════
    std::string esc = escapeMermaidLabel("gpt-4o \"mini\" & <test>\nline2\r");
    check(esc.find("&quot;mini&quot;") != std::string::npos, "转义：引号 → &quot;");
    check(esc.find("&amp;") != std::string::npos, "转义：& → &amp;");
    check(esc.find("&lt;test&gt;") != std::string::npos, "转义：< > → 实体");
    check(esc.find("<br/>line2") != std::string::npos, "转义：换行 → <br/>");
    check(esc.find('\r') == std::string::npos, "转义：\\r 被丢掉");
    check(escapeMermaidLabel("&amp;") == "&amp;amp;",
          "转义：输入 &amp; → &amp;amp;（逐字符，无二次转换顺序坑）",
          escapeMermaidLabel("&amp;"));
    check(escapeMermaidLabel("plain").empty() == false
          && escapeMermaidLabel("plain") == "plain", "转义：普通文本原样");

    // ═══ 2. flowchart ═════════════════════════════════════════════════
    Run r = makeRun();
    std::string mmd = emitMermaid(r);

    check(mmd.find("flowchart TD\n") == 0, "flowchart：首行正确");
    check(mmd.find("s0[\"Step 0 · gpt-4o-mini · 0.18s · 15 tok\"]") != std::string::npos,
          "flowchart：step 节点标签含序号/模型/耗时/token", mmd.substr(0, 120));
    check(mmd.find("s1[\"Step 1 · gpt-4o · 0.20s · token —\"]") != std::string::npos,
          "flowchart：未观测 token 显示 — 而非 0");
    check(countOf(mmd, "[\"") == 5, "flowchart：2 step + 3 tool 共 5 个节点",
          "count=" + std::to_string(countOf(mmd, "[\"")));
    check(countOf(mmd, "s0 --> t0_") == 3, "flowchart：3 条 step→tool 边");
    check(countOf(mmd, " --> s1") == 3, "flowchart：3 个工具汇聚到下一步（不是 step 直连）");
    check(mmd.find("s0 --> s1") == std::string::npos,
          "flowchart：有工具的步不直连下一步（避免与工具同层）");
    check(mmd.find("read &quot;file&quot; &amp; &lt;x&gt;") != std::string::npos,
          "flowchart：工具名走转义，不破坏节点语法");
    check(mmd.find("· 0.12s ~") != std::string::npos, "flowchart：推断耗时带 ~ 标记");
    check(mmd.find("classDef failed fill:#f96,stroke:#333") != std::string::npos
          && mmd.find("classDef warn fill:#f8c471,stroke:#333") != std::string::npos,
          "flowchart：两种失败样式已定义");
    check(mmd.find("class t0_1 failed") != std::string::npos
          && mmd.find("class t0_2 warn") != std::string::npos,
          "flowchart：confirmed→失败红，inferred→琥珀");
    check(mmd.find("class t0_0") == std::string::npos, "flowchart：成功工具不着色");
    check(mmd.find("classDef") > mmd.find(" --> s1"), "flowchart：样式段在边之后");

    // 无失败 + 空 Run
    Run clean = makeRun();
    for (auto &s : clean.steps) for (auto &t : s.tools) t.error = ErrorKind::none;
    check(emitMermaid(clean).find("classDef") == std::string::npos,
          "flowchart：无失败不输出 classDef");

    Run noTools = makeRun();
    noTools.steps[0].tools.clear();
    check(emitMermaid(noTools).find("s0 --> s1") != std::string::npos,
          "flowchart：无工具的步直连下一步");

    check(emitMermaid(Run{}) == "flowchart TD\n", "flowchart：空 Run 只输出首行");

    // ═══ 3. gantt ═════════════════════════════════════════════════════
    std::string gantt = emitTimeline(r);
    check(gantt.find("gantt\n") == 0, "gantt：首行正确");
    check(gantt.find("dateFormat x") != std::string::npos, "gantt：毫秒时间轴");
    check(gantt.find("section Step 0") != std::string::npos
          && gantt.find("section Step 1") != std::string::npos, "gantt：两个 section");
    check(gantt.find("s0 :done, 0, 180") != std::string::npos,
          "gantt：step 区间用相对时间（1000→1180 变 0→180）", gantt.substr(0, 160));
    check(gantt.find("search :done, 180, 300") != std::string::npos,
          "gantt：工具结束 = startMs + durationMs（1180+120=1300 → 300）");
    check(countOf(gantt, " :crit,") == 2, "gantt：两个失败工具标 crit");
    check(gantt.find("s1 :active, 200, 400") != std::string::npos,
          "gantt：未完成的 step 标 active");
    check(emitTimeline(Run{}).find("gantt\n") == 0, "gantt：空 Run 不崩");

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
