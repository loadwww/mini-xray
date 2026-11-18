// T7.3 验证台：spawnWithEnv（真起子进程，测环境注入 + 退出码透传 + 参数转义）
//
// 跑法：./build/spawn_test.exe
// 期望：全部 [ ok ]，末行 "N passed, 0 failed"，退出码 0。

#include "proxy/spawn.h"

#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace mxr;

static int g_pass = 0;
static int g_fail = 0;

static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

static void check(bool ok, const std::string &what, const std::string &detail = "")
{
    if (ok) { ++g_pass; std::cout << "[ ok ] " << what << "\n"; }
    else { ++g_fail; std::cout << "[FAIL] " << what; if (!detail.empty()) std::cout << "   << " << detail; std::cout << "\n"; }
}

int main()
{
    std::string err;
    int code = -999;

    // ═══ 1. 空命令 ════════════════════════════════════════════════
    {
        err.clear();
        bool ok = spawnWithEnv({}, {}, code, err);
        check(!ok && !err.empty(), "空命令返回 false 且写错误原因", err);
    }

    // ═══ 2. 退出码透传 ════════════════════════════════════════════
#ifdef _WIN32
    {
        err.clear();
        bool ok = spawnWithEnv({"cmd", "/c", "exit 0"}, {}, code, err);
        check(ok && code == 0, "子进程退出码 0 透传", "code=" + std::to_string(code) + " err=" + err);
    }
    {
        err.clear();
        bool ok = spawnWithEnv({"cmd", "/c", "exit 7"}, {}, code, err);
        check(ok && code == 7, "子进程非零退出码 7 透传（spawn 仍成功）",
              "code=" + std::to_string(code) + " err=" + err);
    }

    // ═══ 3. 环境变量注入 ══════════════════════════════════════════
    // 让子进程把注入的变量值写进文件，再读回断言（stdout 抓不到，用文件中转）
    {
        const std::string outFile = "runs/spawn_test_env.txt";
        std::map<std::string, std::string> env = {
            {"OPENAI_BASE_URL", "http://127.0.0.1:8080/v1"},
            {"MXR_PROBE_KEY", "probe-value-42"}
        };
        // cmd 的 echo %VAR% > file；路径用相对，工作目录继承父进程（build/）
        std::string script = "echo %MXR_PROBE_KEY% > " + outFile
                           + " & echo %OPENAI_BASE_URL% >> " + outFile;
        err.clear();
        bool ok = spawnWithEnv({"cmd", "/c", script}, env, code, err);
        std::ifstream in(outFile);
        std::string line1, line2;
        std::getline(in, line1);
        std::getline(in, line2);
        // 去掉可能的 \r
        line1 = trim(line1); line2 = trim(line2);
        check(ok && line1 == "probe-value-42", "注入 MXR_PROBE_KEY 生效", "got=[" + line1 + "] err=" + err);
        check(line2 == "http://127.0.0.1:8080/v1", "注入 OPENAI_BASE_URL 生效", "got=[" + line2 + "]");
    }

    // ═══ 4. 覆盖已有环境变量 ══════════════════════════════════════
    {
        const std::string outFile = "runs/spawn_test_override.txt";
        std::map<std::string, std::string> env = {{"MXR_OVERRIDE", "new-value"}};
        std::string script = "echo %MXR_OVERRIDE% > " + outFile;
        err.clear();
        bool ok = spawnWithEnv({"cmd", "/c", script}, env, code, err);
        std::ifstream in(outFile);
        std::string line;
        std::getline(in, line);
        line = trim(line);
        check(ok && line == "new-value", "覆盖环境变量生效", "got=[" + line + "]");
    }

    // ═══ 5. 参数转义（含空格的参数）════════════════════════════════
    {
        // cmd /c echo 一个含空格的字符串 —— 如果他没正确 quoteArg，会被拆成多个参数导致失败
        err.clear();
        bool ok = spawnWithEnv({"cmd", "/c", "echo hello"}, {}, code, err);
        check(ok && code == 0, "含空格的命令执行成功", "code=" + std::to_string(code) + " err=" + err);
    }

    // ═══ 6. 启动失败（不存在的程序）════════════════════════════════
    {
        err.clear();
        bool ok = spawnWithEnv({"__mxr_no_such_program_xyz__"}, {}, code, err);
        check(!ok && !err.empty(), "不存在的程序返回 false 且写错误原因", err);
    }
#endif

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
