// T4.4 验证台：落盘 → 重放 → 对账（不联网、不起代理进程）
//
// 跑法：./build/replay_test.exe
// 期望：全部 [ ok ]，末行 "N passed, 0 failed"，退出码 0。
//
// 这一关的四块代码在这里被串成一条链：
//   SseRunLogger(②) → Event/toJson(①) → Recorder → events.jsonl
//        → replayFile(④) → 新建 SseAccumulator → 与 sse_end 对账
//   ③（server.cpp 里的三处钩子）不在这里测，靠端到端 curl 验。

#include "cli/replay.h"
#include "parse/sse.h"
#include "store/event.h"
#include "store/recorder.h"
#include "store/sse_run.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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

static const std::string kTmp = "runs/replay_test_events.jsonl";

// 一条和 mock 形状相同、但只有 2 个内容块的短流
static std::string shortStream()
{
    return "data: {\"choices\":[{\"delta\":{\"content\":\"ab \"},\"finish_reason\":null}]}\n\n"
           "data: {\"choices\":[{\"delta\":{\"content\":\"cd \"},\"finish_reason\":null}]}\n\n"
           "data: [DONE]\n\n";
}

static void clean(const std::string &path)
{
    std::filesystem::remove(path);
}

// 用记录员写一份文件：把整条流切成 pieces 块喂进去，可选是否收尾
static void buildRun(const std::string &path, const std::string &hop,
                     const std::vector<std::string> &pieces, bool withEnd,
                     bool append = false)
{
    if (!append) {
        clean(path);
    }
    mxr::Recorder rec(path);
    auto log = std::make_shared<mxr::SseRunLogger>(rec, hop);
    for (const auto &p : pieces) {
        log->onChunk(p);
    }
    if (withEnd) {
        log->onEnd();
    }
    rec.flush();
}

// 把文件里的某一条 sse_end / sse_chunk 改掉（用于制造不一致）
static void mutate(const std::string &path, const std::string &type,
                   const std::string &key, const nlohmann::json &value)
{
    std::ifstream in(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        auto j = nlohmann::json::parse(line, nullptr, false);
        if (!j.is_discarded() && j.value("type", "") == type) {
            j[key] = value;
            lines.push_back(j.dump());
        } else {
            lines.push_back(line);
        }
    }
    in.close();
    std::ofstream out(path, std::ios::trunc);
    for (const auto &l : lines) {
        out << l << "\n";
    }
}

int main()
{
    std::filesystem::create_directories("runs");
    const std::string s = shortStream();

    // ── 1. 完整流：任意切片 → 落盘 → 重放一致 ────────────────────
    {
        std::vector<std::string> pieces = {s.substr(0, 17), s.substr(17, 60), s.substr(77)};
        buildRun(kTmp, "h-1", pieces, true);
        std::ostringstream os;
        const auto sum = mxr::replayFile(kTmp, os);
        check(sum.opened, "完整流：文件打开成功");
        check(sum.streams == 1, "完整流：见到 1 条 sse_end", std::to_string(sum.streams));
        check(sum.chunks == 3, "完整流：3 条 sse_chunk", std::to_string(sum.chunks));
        check(sum.mismatches == 0 && sum.incomplete == 0,
              "完整流：文本/usage 对账一致，无残留",
              "mismatch=" + std::to_string(sum.mismatches)
                  + " incomplete=" + std::to_string(sum.incomplete));
        check(os.str().find("replay:") != std::string::npos, "完整流：打印了汇总行");
    }

    // ── 2. 落盘内容检查：字段形状 ─────────────────────────────────
    {
        std::ifstream in(kTmp);
        std::string line;
        int chunkCount = 0, endCount = 0;
        long long maxIndex = -1;
        std::string endText;
        while (std::getline(in, line)) {
            auto j = nlohmann::json::parse(line, nullptr, false);
            if (j.is_discarded()) continue;
            if (j.value("type", "") == "sse_chunk") {
                ++chunkCount;
                maxIndex = std::max(maxIndex, j.value("chunk_index", -1LL));
            } else if (j.value("type", "") == "sse_end") {
                ++endCount;
                endText = j.value("text", "");
            }
        }
        check(chunkCount == 3 && maxIndex == 2, "落盘：chunk_index 从 0 递增到 2",
              "count=" + std::to_string(chunkCount) + " max=" + std::to_string(maxIndex));
        check(endCount == 1 && endText == "ab cd ", "落盘：sse_end 的 text 正确",
              "[" + endText + "]");
    }

    // ── 3. sse_end 被篡改 → 对账必须报错 ──────────────────────────
    {
        buildRun(kTmp, "h-1", {s.substr(0, 17), s.substr(17)}, true);
        mutate(kTmp, "sse_end", "text", "FAKE ANSWER");
        std::ostringstream os;
        const auto sum = mxr::replayFile(kTmp, os);
        check(sum.mismatches == 1, "篡改：文本不一致被抓到",
              std::to_string(sum.mismatches));
    }

    // ── 4. chunk_index 跳号 → 顺序问题被抓到 ──────────────────────
    {
        buildRun(kTmp, "h-1", {s.substr(0, 17), s.substr(17, 60), s.substr(77)}, true);
        mutate(kTmp, "sse_chunk", "chunk_index", 99);
        std::ostringstream os;
        const auto sum = mxr::replayFile(kTmp, os);
        check(sum.mismatches >= 1, "乱序：chunk_index 跳号被抓到",
              std::to_string(sum.mismatches));
    }

    // ── 5. 进程被杀 / 客户端断开：没有 sse_end → incomplete ───────
    {
        buildRun(kTmp, "h-9", {s.substr(0, 17), s.substr(17, 60)}, false);
        std::ostringstream os;
        const auto sum = mxr::replayFile(kTmp, os);
        check(sum.incomplete == 1 && sum.streams == 0,
              "半截流：报 incomplete，不算成一条完整流",
              "incomplete=" + std::to_string(sum.incomplete)
                  + " streams=" + std::to_string(sum.streams));
    }

    // ── 6. 两条流共存，靠 hop 分开 ────────────────────────────────
    {
        buildRun(kTmp, "h-A", {s}, true);
        buildRun(kTmp, "h-B", {s.substr(0, 30), s.substr(30)}, true, true);
        std::ostringstream os;
        const auto sum = mxr::replayFile(kTmp, os);
        check(sum.streams == 2 && sum.chunks == 3 && sum.mismatches == 0
                  && sum.incomplete == 0,
              "多流：两条 hop 各自对账通过",
              "streams=" + std::to_string(sum.streams)
                  + " chunks=" + std::to_string(sum.chunks));
    }

    // ── 7. 坏行 / 无关事件不能让 replay 崩 ────────────────────────
    {
        buildRun(kTmp, "h-1", {s}, true);
        {
            std::ofstream out(kTmp, std::ios::app);
            out << "这行不是 JSON\n\n";
            out << "{\"type\":\"req\",\"hop\":\"h-zz\"}\n";
        }
        std::ostringstream os;
        const auto sum = mxr::replayFile(kTmp, os);
        check(sum.opened && sum.streams == 1 && sum.mismatches == 0,
              "容错：坏行与无关事件被跳过");
    }

    // ── 8. 文件不存在 ────────────────────────────────────────────
    {
        clean("runs/definitely_not_here.jsonl");
        std::ostringstream os;
        const auto sum = mxr::replayFile("runs/definitely_not_here.jsonl", os);
        check(!sum.opened, "缺文件：opened == false");
    }

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
