#include "cli.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#include "cli/replay.h"
#include "common/version.h"
#include "config/config.h"
#include "proxy/route.h"
#ifdef _WIN32
#include "proxy/runner.h"
#endif
#include "proxy/server.h"
#include "cli/finish.h"
#include "commands.h"
#include "cli/selftest.h"
#include "store/jsonl_writer.h"

namespace {

// 唯一一份使用说明。help 走 stdout（用户主动要的），报错走 stderr（诊断流）——
// 同一段文字、两种去向，所以参数是 ostream&，不能写死 cout 或 cerr
void printUsage(std::ostream& os) {
    os << "usage: mxr [--verbose] [--root <dir>] <command> [args...]\n"
          "\n"
          "commands:\n"
          "  version            打印版本号\n"
          "  cfg <path>         加载配置并以 JSON 打印\n"
          "  serve [cfg]        启动反向代理（默认读 config.json）\n"
          "  echo <text>        向 <root>/events.jsonl 追加一条记录\n"
          "  replay <file>      只读 events.jsonl，重建 SSE 流并与 sse_end 对账\n"
          "  show <run-dir>     打印 run 摘要（步数/耗时/工具表）\n"
          "  list [--root <dir>] [--json]   列出会话（按时间倒序）\n"
          "  diff <A> <B>       对齐两个 run，报告首个分叉点\n"
          "  run -- <cmd...>    起代理 + 子进程（仅 Windows；Linux 用 serve + finish）\n"
          "  finish <session>  从 runs/<id>/events.jsonl 重建报告包\n"
          "  prune              清理旧会话（暂未实现）\n"
          "  selftest [name...] 运行全部/指定验证台（--list 列出）\n"
          "  help               显示本说明\n";
}

// 每个子命令单独的用法：cmd --help 时走这里，退出码 0（用户主动问的）
void printCommandHelp(std::ostream& os, const std::string& cmd) {
    os << "usage: mxr " << cmd << " ...\n";
    if (cmd == "show")       os << "  show <run-dir>   读取目录下的 run.json 并打印摘要\n";
    else if (cmd == "list")  os << "  list [--root <dir>] [--json]   扫 meta.json 列出会话，按时间倒序\n";
    else if (cmd == "diff")  os << "  diff <A> <B>     按步序号对齐两个 run，输出首个分叉点与字段变化\n";
    else if (cmd == "run")   os << "  run -- <cmd...>  起代理并运行子进程（仅 Windows）\n";
    else if (cmd == "finish") os << "  finish <sessionId>  从 runs/<id>/events.jsonl 重建报告包\n";
    else if (cmd == "prune") os << "  prune            清理旧会话（暂未实现）\n";
    else if (cmd == "selftest") os << "  selftest [name...] [--list]  串行跑验证台，汇总退出码\n";
}

// 退出码表：脚本靠它判断成败，三个值各有明确含义，别混用
constexpr int kExitOk    = 0;   // 命令完成（哪怕伴随 warning，比如配置降级）
constexpr int kExitFail  = 1;   // 运行时失败：要做的事没做成
constexpr int kExitUsage = 2;   // 用法错：用户敲错了

struct GlobalOpts {
    bool        verbose = false;
    std::string root    = ".";
};

} // namespace

int mxr::run(int argc, char* argv[]) {
    try {
        GlobalOpts g;

        // ── 第一段：摘全局 flag。约定：全局 flag 只能出现在子命令之前
        int i = 1;
        while (i < argc && argv[i][0] == '-') {
            const std::string opt = argv[i];

            if (opt == "--verbose") {
                g.verbose = true;                  // 填一个布尔值
                ++i;
            } else if (opt == "--root") {
                if (i + 1 >= argc) {                // --root 后面没跟东西
                    std::cerr << "mxr: --root 需要一个目录参数\n";
                    printUsage(std::cerr);
                    return kExitUsage;
                }
                g.root = argv[i+1];                     // 取下一个参数（argv[i+1]）
                i += 2;
            } else {
                std::cerr << "mxr: 未知选项 " << opt << "\n";
                printUsage(std::cerr);
                return kExitUsage;
            }
        }

        // ── 第二段：分派子命令
        if (i >= argc) {
            printUsage(std::cerr);                  // 只敲了 mxr，或只敲了 flag
            return kExitUsage;
        }
        const std::string cmd = argv[i];

        if (cmd == "version") {
            std::cout << "mxr " << versionString() << "\n";
            return kExitOk;
        }
        if (cmd == "help") {
            printUsage(std::cout);                  // 同一份文本，走去 stdout
            return kExitOk;
        }
        if (cmd == "cfg") {
            if (i + 1 >= argc) {
                std::cerr << "mxr: cfg 需要一个配置文件路径\n";
                printUsage(std::cerr);
                return kExitUsage;
            }
            std::optional<Config> cfg = loadConfig(argv[i + 1]);
            if (cfg == std::nullopt) {
                std::cerr << "配置文件格式有误\n";
                return kExitFail;
            }
            std::cout << toJsonString(*cfg) << "\n";
            return kExitOk;
        }
        if (cmd == "serve") {
            // 配置文件路径：`mxr serve` 用默认的 config.json，`mxr serve xxx.json` 用指定的
            const std::string cfgPath = (i + 1 < argc) ? argv[i + 1] : "config.json";

            // 启动阶段两道校验：先读得出来，再校验上游配置是否说得通
            std::optional<Config> cfg=loadConfig(cfgPath);
            if(cfg==std::nullopt){
                std::cerr << "配置文件格式有误\n";
                return kExitFail;
            }
            // 进了 serve 就出不来：它阻塞在自己的 listen 循环里
            std::optional<std::string> err=validateUpstreams(cfg->proxy.upstreams);
            if(err!=std::nullopt){
                std::cerr << "fatal: " << *err << "\n"; return kExitFail;
            }
            return serve(*cfg);
        }

        if (cmd == "echo") {
            if (i + 1 >= argc) {
                std::cerr << "mxr: echo 缺少要写入的文本\n";
                printUsage(std::cerr);
                return kExitUsage;
            }
            std::string path = g.root + "/events.jsonl";
            mxr::JsonlWriter w(path);
            w.append({{"text", argv[i + 1]}});
            return kExitOk;
        }

        if (cmd == "replay") {
            if (i + 1 >= argc) {
                std::cerr << "mxr: replay 需要一个 events.jsonl 路径\n";
                printUsage(std::cerr);
                return kExitUsage;
            }
            return replay(argv[i + 1]) == 0 ? kExitOk : kExitFail;
        }

        // cmdXxx 只收"子命令之后"的参数（不含程序名和子命令名）
        std::vector<std::string> subArgs(argv + i + 1, argv + argc);

        // 子命令的 --help：用户主动问，走 stdout + 退出码 0
        if (std::find(subArgs.begin(), subArgs.end(), "--help") != subArgs.end()
            || std::find(subArgs.begin(), subArgs.end(), "-h") != subArgs.end()) {
            printCommandHelp(std::cout, cmd);
            return kExitOk;
        }

        if (cmd == "show") {
            return cmdShow(subArgs);
        }
        if (cmd == "list") {
            // 子命令没显式 --root 时，仅当全局 --root 被显式改过（非默认 "."）才注入。
            // 全局默认 "." 是给 echo 的；list 自己的默认值 "runs" 不能被它覆盖。
            if (g.root != "." && std::find(subArgs.begin(), subArgs.end(), "--root") == subArgs.end())
                subArgs.insert(subArgs.begin(), {"--root", g.root});
            return cmdList(subArgs);
        }
        if (cmd == "diff") {
            return cmdDiff(subArgs);
        }
        if (cmd == "run") {
#ifndef _WIN32
            std::cerr << "mxr run: 当前平台未实现子进程编排；请用 serve + finish\n";
            return kExitUsage;
#else
            // 约定：mxr run -- <cmd...>，"--" 之后的内容原样交给子进程。
            if (subArgs.empty() || subArgs[0] != "--") {
                std::cerr << "mxr run: 用法 mxr run -- <command...>\n";
                return kExitUsage;
            }
            std::vector<std::string> childCmd(subArgs.begin() + 1, subArgs.end());
            if (childCmd.empty()) {
                std::cerr << "mxr run: -- 之后需要一条命令\n";
                return kExitUsage;
            }
            std::optional<Config> cfg = loadConfig("config.json");
            if (cfg == std::nullopt) {
                std::cerr << "mxr run: 配置文件格式有误\n";
                return kExitFail;
            }
            int childExit = -1;
            std::string err;
            if (!runTarget(*cfg, childCmd, childExit, err)) {
                std::cerr << "mxr run: " << err << "\n";
                return kExitFail;
            }
            return childExit;  // 子进程退出码透传（崩溃时也已完成落盘）
#endif
        }
        if (cmd == "finish") {
            if (subArgs.size() != 1) {
                std::cerr << "mxr finish: 用法 mxr finish <sessionId>\n";
                return kExitUsage;
            }
            std::string err;
            if (!finishRun(g.root, subArgs[0], err)) {
                std::cerr << "mxr finish: " << err << "\n";
                return kExitFail;
            }
            return kExitOk;
        }
        if (cmd == "selftest") {
            // --list 走 stdout；否则把剩余参数当验证台名（空 = 全部）
            if (std::find(subArgs.begin(), subArgs.end(), "--list") != subArgs.end()) {
                selftestList();
                return kExitOk;
            }
            std::vector<std::string> testNames;
            for (const auto &a : subArgs)
                if (a != "--list") testNames.push_back(a);
            // 验证台 exe 在 mxr.exe 同目录，用 argv[0] 定位（不依赖当前工作目录）
            std::string baseDir = std::filesystem::path(argv[0]).parent_path().string();
            if (baseDir.empty()) baseDir = ".";
            SelftestSummary sum = runSelftest(baseDir, testNames);
            return sum.failed == 0 ? kExitOk : kExitFail;
        }
        if (cmd == "prune") {
            std::cerr << "mxr prune: 暂未实现\n";
            return kExitFail;
        }

        std::cerr << "mxr: 未知命令 " << cmd << "\n";
        printUsage(std::cerr);
        return kExitUsage;

    } catch (const std::exception& e) {
        // 兜底：模块抛的异常在这里变成"一句话 + 失败码"，绝不穿到 main 之外
        std::cerr << "fatal: " << e.what() << "\n";
        return kExitFail;
    }
}
