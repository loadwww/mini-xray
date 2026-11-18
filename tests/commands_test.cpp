// T7.2 验证台：CLI 子命令（直接调 cmdXxx，抓 stdout/stderr 断言）
//
// 跑法：./build/commands_test.exe
// 期望：全部 [ ok ]，末行 "N passed, 0 failed"，退出码 0。

#include "cli/commands.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using namespace mxr;
namespace fs = std::filesystem;

static int g_pass = 0;
static int g_fail = 0;

static void check(bool ok, const std::string &what, const std::string &detail = "")
{
    if (ok) { ++g_pass; std::cout << "[ ok ] " << what << "\n"; }
    else { ++g_fail; std::cout << "[FAIL] " << what; if (!detail.empty()) std::cout << "   << " << detail; std::cout << "\n"; }
}

// 抓 cmdXxx 写到 stdout 的内容
struct Capture {
    std::ostringstream buf;
    std::streambuf *old;
    Capture() : old(std::cout.rdbuf(buf.rdbuf())) {}
    ~Capture() { std::cout.rdbuf(old); }
    std::string str() const { return buf.str(); }
};

static void putFile(const fs::path &p, const std::string &s)
{
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}

// 造一个 run.json（字段对齐 model.cpp 的 to_json）
static std::string makeRunJson(const std::string &id, int stepCount, int modelSwitch)
{
    std::string steps;
    for (int i = 0; i < stepCount; ++i)
    {
        if (i) steps += ",";
        std::string model = (i == 1 && modelSwitch) ? "gpt-4o-mini" : "gpt-4o";
        long long tok = 10 + i;
        steps += "{\"index\":" + std::to_string(i)
               + ",\"model\":\"" + model + "\""
               + ",\"start_ms\":" + std::to_string(1000 + i * 200)
               + ",\"end_ms\":" + std::to_string(1180 + i * 200)
               + ",\"usage\":{\"total_tokens\":" + std::to_string(tok) + ",\"observed\":true}"
               + ",\"output_text\":\"hi\"";
        if (i == 0)
            steps += ",\"tools\":[{\"name\":\"search\",\"args_json\":\"{\\\"q\\\":\\\"a\\\"}\",\"error\":\"none\",\"duration_ms\":120},"
                     "{\"name\":\"calc\",\"args_json\":\"{}\",\"error\":\"confirmed\",\"duration_ms\":120}]";
        else
            steps += ",\"tools\":[]";
        steps += "}";
    }
    return "{\"id\":\"" + id + "\",\"steps\":[" + steps + "],\"start_ms\":1000,\"end_ms\":2000}";
}

int main()
{
    const fs::path root = "runs/commands_test";
    fs::remove_all(root);
    fs::create_directories(root / "base");
    fs::create_directories(root / "cur");

    putFile(root / "base" / "run.json", makeRunJson("run-base", 3, 0));
    putFile(root / "cur" / "run.json", makeRunJson("run-cur", 3, 1)); // Step1 模型不同
    putFile(root / "base" / "meta.json",
            R"({"id":"run-base","start_ms":1000,"step_count":3,"total_tokens":33,"session_id":"base"})");
    putFile(root / "cur" / "meta.json",
            R"({"id":"run-cur","start_ms":2000,"step_count":3,"total_tokens":33,"session_id":"cur"})");
    // 一个坏 meta（容错用例）
    fs::create_directories(root / "broken");
    putFile(root / "broken" / "meta.json", "{ bad json");

    // ═══ 1. cmdShow ════════════════════════════════════════════════
    {
        Capture c;
        int rc = cmdShow({(root / "base").string()});
        std::string out = c.str();
        check(rc == 0, "show：合法 run 返回 0", "rc=" + std::to_string(rc));
        check(out.find("run-base") != std::string::npos, "show：打印 run id", out.substr(0, 80));
        check(out.find("3 Step") != std::string::npos, "show：步数汇总");
        check(out.find("S0") != std::string::npos && out.find("search") != std::string::npos,
              "show：打印步骤与工具");
    }
    {
        Capture c;
        int rc = cmdShow({});
        c.str(); // 丢弃（错误走 stderr）
        check(rc == 2, "show：缺参数返回 2（用法错）", "rc=" + std::to_string(rc));
    }
    {
        Capture c;
        int rc = cmdShow({(root / "nonexistent").string()});
        check(rc == 1, "show：目标不存在返回 1（运行时失败）", "rc=" + std::to_string(rc));
    }
    {
        Capture c;
        int rc = cmdShow({(root / "broken").string()});
        check(rc == 1, "show：坏 run.json 返回 1", "rc=" + std::to_string(rc));
    }

    // ═══ 2. cmdList ════════════════════════════════════════════════
    {
        Capture c;
        int rc = cmdList({"--root", root.string(), "--json"});
        std::string out = c.str();
        check(rc == 0, "list：返回 0", "rc=" + std::to_string(rc));
        auto j = nlohmann::json::parse(out, nullptr, false);
        check(!j.is_discarded() && j.is_array(), "list --json：输出是合法 JSON 数组",
              out.substr(0, 80));
        check(!j.is_discarded() && j.is_array() && j.size() == 2,
              "list --json：坏 meta 被跳过，剩 2 项",
              "size=" + std::to_string(j.is_array() ? j.size() : 0));
        if (!j.is_discarded() && j.is_array() && j.size() >= 1)
            check(j[0].value("dir", "") == "cur", "list --json：按 start_ms 倒序（cur 在前）",
                  j[0].value("dir", ""));
    }
    {
        Capture c;
        int rc = cmdList({"--root", root.string()});
        std::string out = c.str();
        check(rc == 0 && out.find("cur") != std::string::npos && out.find("base") != std::string::npos,
              "list：默认输出含两个目录名", out.substr(0, 80));
    }
    {
        Capture c;
        int rc = cmdList({"--root", (root / "nonexistent").string(), "--json"});
        check(rc == 0, "list：root 不存在仍返回 0（空列表）", "rc=" + std::to_string(rc));
    }

    // ═══ 3. cmdDiff ════════════════════════════════════════════════
    {
        Capture c;
        int rc = cmdDiff({(root / "base").string(), (root / "cur").string()});
        std::string out = c.str();
        check(rc == 0, "diff：返回 0", "rc=" + std::to_string(rc));
        check(out.find("Step 1") != std::string::npos, "diff：定位分叉点 Step 1", out.substr(0, 120));
        check(out.find("gpt-4o") != std::string::npos, "diff：显示模型变化");
    }
    {
        Capture c;
        int rc = cmdDiff({(root / "base").string(), (root / "base").string()});
        std::string out = c.str();
        check(rc == 0 && out.find("一致") != std::string::npos, "diff：相同 run 报一致", out.substr(0, 80));
    }
    {
        Capture c;
        int rc = cmdDiff({(root / "base").string()});
        check(rc == 2, "diff：缺参数返回 2", "rc=" + std::to_string(rc));
    }
    {
        Capture c;
        int rc = cmdDiff({(root / "base").string(), (root / "nonexistent").string()});
        check(rc == 1, "diff：目标不存在返回 1", "rc=" + std::to_string(rc));
    }
    // token 不同（模型相同）→ 必须报 token 变化（堵住"忽略 token"的改坏盲区）
    fs::create_directories(root / "moretok");
    {
        std::string j = makeRunJson("run-moretok", 3, 0);
        // 把 Step0 的 total_tokens 从 10 改成 99（只改一处，模型不变）
        std::size_t p = j.find("\"total_tokens\":10");
        if (p != std::string::npos) j.replace(p, 17, "\"total_tokens\":99");
        putFile(root / "moretok" / "run.json", j);
    }
    {
        Capture c;
        int rc = cmdDiff({(root / "base").string(), (root / "moretok").string()});
        std::string out = c.str();
        check(rc == 0 && out.find("tokens: 10 -> 99") != std::string::npos,
              "diff：仅 token 不同也报分叉", out.substr(0, 120));
    }
    // 步数不同 → 新增/删除
    fs::create_directories(root / "short");
    putFile(root / "short" / "run.json", makeRunJson("run-short", 1, 0));
    {
        Capture c;
        int rc = cmdDiff({(root / "base").string(), (root / "short").string()});
        std::string out = c.str();
        check(rc == 0 && (out.find("删除") != std::string::npos || out.find("Step 1") != std::string::npos),
              "diff：步数不同报删除/分叉", out.substr(0, 120));
    }

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
