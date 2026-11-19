// T8.2 验证台：fixture 回归（一套通用逻辑跑所有 fixture 目录）
//
// 跑法：./build/fixture_test.exe
// 期望：全部 [ ok ]，末行 "N passed, 0 failed"，退出码 0。
//
// 加新场景：只在 tests/fixtures/ 下加一个目录（events.jsonl + expected.json），
// 然后在 main 里加一行 CHECK_FIXTURE("t2")。测试代码不用改。

#include "chain/link.h"
#include "chain/superstep.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace mxr;
namespace fs = std::filesystem;
using json = nlohmann::json;

// fixture 在源码树的 tests/fixtures/ 下，而 exe 在 build/ 下。
// 从 argv[0]（build/fixture_test.exe）往上两级拿项目根，不依赖当前工作目录。
static fs::path g_root;

static int g_pass = 0;
static int g_fail = 0;

static void check(bool ok, const std::string &what, const std::string &detail = "")
{
    if (ok) { ++g_pass; std::cout << "[ ok ] " << what << "\n"; }
    else { ++g_fail; std::cout << "[FAIL] " << what; if (!detail.empty()) std::cout << "   << " << detail; std::cout << "\n"; }
}

static std::string readAll(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::string();
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// 跑完整还原链，算出摘要 JSON。这是 fixture 的"实际值"。
// 链路：readEvents → pairHops → buildRuns → mergeSupersteps → 统计。
static json runChain(const std::string &dir)
{
    auto events = readEvents((fs::path(dir) / "events.jsonl").string());
    auto pair = pairHops(events);
    auto runs = buildRuns(pair.hops);
    mergeSupersteps(runs, pair.hops);

    int steps = 0, tools = 0, failed = 0;
    long long tokens = 0;
    json runSteps = json::array(), runStarts = json::array();
    json stepIndexes = json::array(), stepModels = json::array();
    json stepTokens = json::array(), stepOutputs = json::array();
    json toolIds = json::array(), toolNames = json::array();
    json toolErrors = json::array(), toolInferred = json::array();

    for (const auto &r : runs) {
        runSteps.push_back((int)r.steps.size());
        runStarts.push_back(r.startMs);
        for (const auto &s : r.steps) {
            ++steps;
            tokens += s.usage.totalTokens;
            stepIndexes.push_back(s.index);
            stepModels.push_back(s.model);
            stepTokens.push_back(s.usage.totalTokens);
            stepOutputs.push_back(s.outputText);
            for (const auto &t : s.tools) {
                ++tools;
                if (t.error != ErrorKind::none) ++failed;
                toolIds.push_back(t.toolCallId);
                toolNames.push_back(t.name);
                toolErrors.push_back(toString(t.error));
                toolInferred.push_back(t.durationInferred);
            }
        }
    }
    json got;
    got["run_count"] = (int)runs.size();
    got["step_count"] = steps;
    got["tool_count"] = tools;
    got["failed"] = failed;
    got["total_tokens"] = tokens;
    got["run_steps"] = runSteps;
    got["run_starts"] = runStarts;
    got["step_indexes"] = stepIndexes;
    got["step_models"] = stepModels;
    got["step_tokens"] = stepTokens;
    got["step_outputs"] = stepOutputs;
    got["tool_ids"] = toolIds;
    got["tool_names"] = toolNames;
    got["tool_errors"] = toolErrors;
    got["duration_inferred"] = toolInferred;
    return got;
}

// 对一个 fixture 目录：读期望 → 跑链路 → 逐字段比对。
static void checkFixture(const std::string &name)
{
    const std::string dir = (g_root / "tests" / "fixtures" / name).string();
    json expected = json::parse(readAll(fs::path(dir) / "expected.json"), nullptr, false);
    check(!expected.is_discarded() && expected.is_object(),
          name + "：expected.json 可解析");
    if (expected.is_discarded()) return;

    json got = runChain(dir);
    // 遍历 expected 里出现的所有键（note 只是说明，不参与断言）。
    // 标量比一次，数组先比长度再逐元素比——失败时能精确定位到第几个 run/step/tool。
    for (auto it = expected.begin(); it != expected.end(); ++it) {
        const std::string key = it.key();
        if (key == "note") continue;
        const std::string label = std::string(name) + "：" + key;
        if (!got.contains(key)) {
            check(false, label, "摘要里没有这个字段");
            continue;
        }
        const json &want = it.value();
        const json &have = got.at(key);
        if (want.is_array() && have.is_array()) {
            check(want.size() == have.size(), label + " 数组长度",
                  "want=" + std::to_string(want.size()) + " got=" + std::to_string(have.size()));
            const std::size_t n = std::min(want.size(), have.size());
            for (std::size_t i = 0; i < n; ++i) {
                check(want[i] == have[i], label + "[" + std::to_string(i) + "]",
                      "want=" + want[i].dump() + " got=" + have[i].dump());
            }
        } else {
            check(want == have, label, "want=" + want.dump() + " got=" + have.dump());
        }
    }
}

int main(int argc, char *argv[])
{
    // 项目根：优先用显式参数，否则从 exe 路径推导（build/ 的上一级）。
    if (argc > 1) g_root = fs::path(argv[1]);
    else {
        fs::path exeDir = fs::absolute(argv[0]).parent_path();
        g_root = exeDir.filename() == "build" ? exeDir.parent_path() : exeDir;
    }

    checkFixture("t1"); // 基线：单 hop 非流式无工具
    checkFixture("t2"); // 多轮建链：3 hop 串成 1 run
    checkFixture("t3"); // system-only 陷阱：拆成 2 run
    checkFixture("t4"); // 工具失败：配对 + confirmed 分级 + 推断耗时

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
