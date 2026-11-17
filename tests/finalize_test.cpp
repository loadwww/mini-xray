// T6.2 验证台：finalize 落盘（纯文件，写 runs/finalize_test/）
//
// 跑法：./build/finalize_test.exe
// 期望：全部 [ ok ]，末行 "N passed, 0 failed"，退出码 0。

#include "store/finalize.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace mxr;
namespace fs = std::filesystem;

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

static std::string readFile(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

static Run makeRun(const std::string &id, long long start, long long end)
{
    Run r;
    r.id = id; r.startMs = start; r.endMs = end;
    Step s0;
    s0.index = 0; s0.startMs = start; s0.endMs = start + 180;
    s0.model = "gpt-4o-mini";
    s0.usage = Usage{10, 5, 15, true};
    s0.complete = true;
    s0.tools = {
        ToolExec{"c1", "search", "{}", "ok", start + 180, 120, ErrorKind::none, true},
        ToolExec{"c2", "calc", "{}", "2", start + 180, 120, ErrorKind::none, true},
    };
    r.steps.push_back(s0);
    Step s1;
    s1.index = 1; s1.startMs = start + 200; s1.endMs = end;
    s1.model = "gpt-4o";
    s1.usage = Usage{0, 0, 0, false};
    s1.complete = true;
    r.steps.push_back(s1);
    return r;
}

int main()
{
    const fs::path root = "runs/finalize_test";
    fs::remove_all(root);              // 干净起步

    FinalizeInput in;
    in.sessionId = "s1";
    in.runs = {makeRun("run-1000-abc-0", 1000, 1400),
               makeRun("run-2000-def-1", 2000, 2400)};
    in.eventsJsonl = "{\"type\":\"req\",\"hop\":\"h1\"}\n";
    in.graphMmd = "flowchart TD\n";
    in.timelineMmd = "gantt\n";
    in.parseFailed = 2;
    in.droppedEvents = 1;

    // ═══ 1. 正常 finalize ═══════════════════════════════════════════
    FinalizeResult r = finalize(root.string(), in);
    check(r.ok && r.error.empty(), "正常：ok=true", r.error);
    check(r.filesWritten == 6, "正常：写入 6 个文件",
          "filesWritten=" + std::to_string(r.filesWritten));

    fs::path d = root / "s1";
    check(fs::exists(d / "events.jsonl"), "文件：events.jsonl 存在");
    check(fs::exists(d / "run.json"), "文件：run.json 存在");
    check(fs::exists(d / "run-1.json"), "文件：run-1.json 存在（第二个 run）");
    check(fs::exists(d / "graph.mmd"), "文件：graph.mmd 存在");
    check(fs::exists(d / "timeline.mmd"), "文件：timeline.mmd 存在");
    check(fs::exists(d / "meta.json"), "文件：meta.json 存在");

    check(readFile(d / "events.jsonl") == in.eventsJsonl,
          "内容：events.jsonl 原样保留");
    check(readFile(d / "graph.mmd") == in.graphMmd, "内容：graph.mmd 原样");

    // meta 自洽
    auto meta = nlohmann::json::parse(readFile(d / "meta.json"));
    check(meta["session_id"] == "s1", "meta：session_id 正确");
    check(meta["start_ms"] == 1000 && meta["end_ms"] == 2400,
          "meta：时间取所有 run 的 min/max");
    check(meta["run_count"] == 2 && meta["main_run_id"] == "run-1000-abc-0",
          "meta：run 数与主 run id");
    check(meta["step_count"] == 4 && meta["tool_count"] == 4,
          "meta：步数=4、工具数=4（与 run.json 自洽）");
    check(meta["total_tokens"] == 30, "meta：总 token=30");
    check(meta["parse_failed"] == 2 && meta["dropped_events"] == 1,
          "meta：降级计数原样带上");

    // run.json 内容
    auto j0 = nlohmann::json::parse(readFile(d / "run.json"));
    check(j0["id"] == "run-1000-abc-0" && j0["steps"].size() == 2,
          "run.json：可逆序列化（id 与步数）");

    // ═══ 2. 幂等：重复 finalize 字节一致 ════════════════════════════
    std::string meta1 = readFile(d / "meta.json");
    FinalizeResult r2 = finalize(root.string(), in);
    check(r2.ok && r2.filesWritten == 6, "幂等：第二次 finalize 成功");
    check(readFile(d / "meta.json") == meta1,
          "幂等：meta.json 字节一致（时间取事件而非 nowMs）");
    check(!fs::exists(d / "meta.json.tmp"), "幂等：无残留 .tmp 文件");

    // ═══ 3. 空 runs ══════════════════════════════════════════════════
    FinalizeInput emptyIn = in;
    emptyIn.sessionId = "empty";
    emptyIn.runs.clear();
    emptyIn.eventsJsonl.clear();
    FinalizeResult r3 = finalize(root.string(), emptyIn);
    check(r3.ok && r3.filesWritten == 5,
          "空 runs：成功写 5 个文件（无 run-1.json）",
          "filesWritten=" + std::to_string(r3.filesWritten));
    auto metaEmpty = nlohmann::json::parse(readFile(root / "empty" / "meta.json"));
    check(metaEmpty["run_count"] == 0 && metaEmpty["step_count"] == 0
          && metaEmpty["main_run_id"] == "" && metaEmpty["start_ms"] == 0,
          "空 runs：meta 全 0/空，不崩");
    check(nlohmann::json::parse(readFile(root / "empty" / "run.json")).is_object(),
          "空 runs：run.json 是空对象");

    // ═══ 4. 安全：非法 sessionId ════════════════════════════════════
    for (const std::string bad : {"../escape", "/abs", "a/b", "..", ""})
    {
        FinalizeInput badIn = in;
        badIn.sessionId = bad;
        FinalizeResult br = finalize(root.string(), badIn);
        check(!br.ok && !br.error.empty(),
              "安全：拒绝 sessionId=\"" + bad + "\"", br.error);
    }
    check(!fs::exists(fs::path("runs") / "escape"), "安全：未在 runs 外创建目录");

    // ═══ 5. 原子写：目标已存在时被覆盖 ══════════════════════════════
    // 上面的幂等测试已经覆盖（第二次 finalize 覆盖同名文件且内容正确）
    check(readFile(d / "run.json").find("run-1000-abc-0") != std::string::npos,
          "原子写：已存在的 run.json 被正确覆盖");

    fs::remove_all(root);              // 清理
    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
