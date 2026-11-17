// T6.3 验证台：Markdown 报告（纯字符串，不联网、不写文件）
//
// 跑法：./build/report_test.exe             跑断言
//       ./build/report_test.exe --dump      打印 fixture 生成的 report.md
// 期望：全部 [ ok ]，末行 "N passed, 0 failed"，退出码 0。

#include "ui/report.h"

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

static std::size_t pos(const std::string &s, const std::string &sub)
{
    return s.find(sub);
}

static Run makeRun()
{
    Run r;
    r.id = "run-1000-abc-0";
    r.startMs = 1000; r.endMs = 1900;

    Step s0;
    s0.index = 0; s0.startMs = 1000; s0.endMs = 1200;
    s0.model = "gpt-4o-mini";
    s0.usage = Usage{10, 5, 15, true};
    s0.complete = true;
    s0.outputText = "第一步的回答";
    s0.tools = {
        ToolExec{"c1", "search | special `", "{}", "line1\nline2", 1200, 120,
                 ErrorKind::none, true},
        ToolExec{"c2", "calc", "{}", std::string(250, 'x'), 1200, 120,
                 ErrorKind::confirmed, true},
    };
    r.steps.push_back(s0);

    Step s1;
    s1.index = 1; s1.startMs = 1250; s1.endMs = 1900;
    s1.model = "gpt-4o";
    s1.usage = Usage{0, 0, 0, false};
    s1.complete = false;
    s1.outputText.clear();
    r.steps.push_back(s1);
    return r;
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "--dump")
    {
        std::cout << emitReport(makeRun());
        return 0;
    }

    std::string md = emitReport(makeRun());

    // ═══ 1. 结构与结论 ═══════════════════════════════════════════════
    check(md.find("# 执行报告\n") == 0, "结构：首行标题");
    check(pos(md, "**结论**：第 0 步开始出问题") != std::string::npos,
          "结论：一句话指出最早失败步", md.substr(0, 120));
    check(pos(md, "## 概览") != std::string::npos, "结构：概览节");
    check(pos(md, "| 项目 | 值 |\n|---|---|") != std::string::npos,
          "结构：概览表头与分隔行");
    check(pos(md, "| 步数 | 2 |") != std::string::npos,
          "概览：步数=2");
    check(pos(md, "| 总耗时 | 0.90s |") != std::string::npos,
          "概览：总耗时（1900-1000=900ms）");
    check(pos(md, "| 总 token | 15 |") != std::string::npos,
          "概览：总 token=15");

    // 失败前置：最早失败节必须在第一个 Step 节之前
    check(pos(md, "## 最早失败") != std::string::npos
          && pos(md, "## 最早失败") < pos(md, "## Step 0"),
          "失败前置：最早失败节在 Step 正文之前");
    check(pos(md, "工具 `calc` 明确失败") != std::string::npos,
          "失败原因：confirmed 工具名点名");
    check(pos(md, "## Step 0") != std::string::npos
          && pos(md, "## Step 1") != std::string::npos,
          "结构：每步一节");

    // ═══ 2. 每步内容 ═════════════════════════════════════════════════
    check(pos(md, "- 模型：gpt-4o-mini") != std::string::npos,
          "每步：模型");
    check(pos(md, "- 耗时：0.20s") != std::string::npos, "每步：耗时");
    check(pos(md, "- token：15 tok") != std::string::npos, "每步：token");
    check(pos(md, "| 工具 | 耗时 | 结果摘要 | 状态 |") != std::string::npos,
          "每步：工具表表头");
    check(pos(md, "| search \\| special \\` | 0.12s ~ | line1 line2 | 成功 |")
              != std::string::npos,
          "工具表：| 与反引号转义、换行折成空格、推断耗时带 ~");
    check(pos(md, "| calc | 0.12s ~ |") != std::string::npos
          && pos(md, "| 失败 |") != std::string::npos,
          "工具表：confirmed 状态=失败");

    // ═══ 3. 截断与空值 ═══════════════════════════════════════════════
    check(md.find(std::string(200, 'x')) != std::string::npos
          && md.find(std::string(201, 'x')) == std::string::npos,
          "截断：250 字符结果截到 200 + ...");
    check(pos(md, "- token：—") != std::string::npos,
          "空值：未观测 token 显示 —");
    check(pos(md, "工具：—") != std::string::npos,
          "空值：无工具的步显示 —");

    // ═══ 4. 成功路径与空 Run ════════════════════════════════════════
    Run clean = makeRun();
    for (auto &s : clean.steps) {
        s.complete = true;
        for (auto &t : s.tools) t.error = ErrorKind::none;
    }
    std::string cleanMd = emitReport(clean);
    check(pos(cleanMd, "**结论**：成功，2 步全部完成") != std::string::npos,
          "成功路径：结论一句成功");
    check(pos(cleanMd, "## 最早失败") == std::string::npos,
          "成功路径：不输出失败节");

    std::string emptyMd = emitReport(Run{});
    check(pos(emptyMd, "# 执行报告") == 0
          && pos(emptyMd, "**结论**：成功，0 步全部完成") != std::string::npos,
          "空 Run：不崩，结论正确");
    check(pos(emptyMd, "| 总 token | — |") != std::string::npos,
          "空 Run：总 token 显示 — 而非 0");

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
