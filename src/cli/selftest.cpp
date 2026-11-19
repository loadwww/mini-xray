#include "cli/selftest.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace mxr {
namespace fs = std::filesystem;

namespace {

// 全部验证台。顺序按 M0→M8 的关卡顺序，失败时一眼看出是哪一关退化的。
const char *const kAllTests[] = {
    "jsonl_test",           // T1.x 文件 I/O + JSONL
    "route_test",           // T2.x 路由匹配
    "recorder_test",        // T3.x 异步记录器
    "chunked_test",         // T4.1 chunked 解码
    "stream_bridge_test",   // T4.2 跨线程流式桥
    "sse_test",             // T4.3 SSE 解析
    "replay_test",          // T4.4 重放对账
    "model_test",           // T5.1 数据模型 JSON 往返
    "extract_test",         // T5.2 请求/响应提取
    "link_test",            // T5.3 建链
    "superstep_test",       // T5.4 超步归并
    "mermaid_test",         // T6.1 Mermaid 出图
    "finalize_test",        // T6.2 落盘
    "report_test",          // T6.3 报告
    "webui_test",           // T7.1 Web UI 路由
    "commands_test",        // T7.2 CLI 子命令
#ifdef _WIN32
    "spawn_test",           // T7.3 子进程（Windows-only：CreateProcessW）
#endif
    "fixture_test",         // T8.2 fixture 回归
};

// 支持简名：selftest sse == selftest sse_test。
// 找得到就返回注册的完整名，找不到返回空串。
std::string resolveName(const std::string &name)
{
    for (const char *t : kAllTests) {
        if (name == t) return t;
        std::string full(t);
        if (full.size() > 5 && full.compare(full.size() - 5, 5, "_test") == 0
            && name == full.substr(0, full.size() - 5))
            return t;
    }
    return std::string();
}

std::string exeName(const std::string &name)
{
#ifdef _WIN32
    return name + ".exe";
#else
    return name;
#endif
}

} // namespace

void selftestList()
{
    std::cout << "可用验证台（mxr selftest <name> 单跑）：\n";
    for (const char *t : kAllTests)
        std::cout << "  " << t << "\n";
}

SelftestSummary runSelftest(const std::string &baseDir,
                            const std::vector<std::string> &names)
{
    SelftestSummary sum;

    std::vector<std::string> toRun;
    if (names.empty()) {
        for (const char *t : kAllTests) toRun.emplace_back(t);
    } else {
        for (const auto &n : names) {
            std::string full = resolveName(n);
            if (!full.empty()) toRun.push_back(full);
            else std::cout << "[SKIP] 未知验证台 " << n << "\n";
        }
    }

    for (const auto &name : toRun) {
        fs::path exe = fs::path(baseDir) / exeName(name);
        if (!fs::exists(exe)) {
            std::cout << "[FAIL] " << name << "  找不到 " << exe.string() << "\n";
            sum.outcomes.push_back({name, false, 0});
            ++sum.failed;
            continue;
        }

        // system() 返回的是"实现定义"的编码；Windows 下就是子进程退出码。
        // 把两个流都丢弃，避免各验证台的诊断输出淹没汇总行。
#ifdef _WIN32
        std::string cmd = "\"" + exe.string() + "\" >nul 2>&1";
#else
        std::string cmd = "\"" + exe.string() + "\" >/dev/null 2>&1";
#endif
        const auto t0 = std::chrono::steady_clock::now();
        int rc = std::system(cmd.c_str());
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();

        bool ok = (rc == 0);
        std::cout << (ok ? "[ ok ] " : "[FAIL] ") << name
                  << "  " << ms << "ms"
                  << (ok ? "" : "  (exit=" + std::to_string(rc) + ")") << "\n";
        sum.outcomes.push_back({name, ok, ms});
        if (ok) ++sum.passed; else ++sum.failed;

        if (!ok) {
            // 失败时再跑一遍、这次不丢弃输出。否则 CI 日志里只剩 exit=1，
            // 看不出是哪条断言红了（验证台的诊断全被上面的 >nul 吞掉了）。
            std::cout.flush();
            const std::string diag = "\"" + exe.string() + "\"";
            std::system(diag.c_str());
        }
    }

    std::cout << "\n" << sum.passed << " passed, " << sum.failed << " failed\n";
    return sum;
}

} // namespace mxr
