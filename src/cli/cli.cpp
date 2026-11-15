#include "cli.h"

#include <exception>
#include <iostream>
#include <ostream>
#include <string>

#include "cli/replay.h"
#include "common/version.h"
#include "config/config.h"
#include "proxy/route.h"
#include "proxy/server.h"
#include "store/jsonl_writer.h"

namespace {

// 唯一一份使用说明。help 走 stdout（用户主动要的），报错走 stderr（诊断流）——
// 同一段文字、两种去向，所以参数是 ostream&，不能写死 cout 或 cerr
void printUsage(std::ostream& os) {
    os << "usage: mxr [--verbose] [--root <dir>] <command> [args...]\n"
          "\n"
          "commands:\n"
          "  version          打印版本号\n"
          "  cfg <path>       加载配置并以 JSON 打印\n"
          "  serve [cfg]      启动反向代理（默认读 config.json）\n"
          "  echo <text>      向 <root>/events.jsonl 追加一条记录\n"
          "  replay <file>    只读 events.jsonl，重建 SSE 流并与 sse_end 对账\n"
          "  help             显示本说明\n";
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

        std::cerr << "mxr: 未知命令 " << cmd << "\n";
        printUsage(std::cerr);
        return kExitUsage;

    } catch (const std::exception& e) {
        // 兜底：模块抛的异常在这里变成"一句话 + 失败码"，绝不穿到 main 之外
        std::cerr << "fatal: " << e.what() << "\n";
        return kExitFail;
    }
}
