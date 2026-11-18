// T7.1 验证台：Web UI 服务端（起真 HTTP 服务，测路由与路径穿越）
//
// 跑法：./build/webui_test.exe
// 期望：全部 [ ok ]，末行 "N passed, 0 failed"，退出码 0。

#include "ui/webui.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <httplib.h>

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

static void putFile(const fs::path &p, const std::string &s)
{
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}

int main()
{
    // 造 root/s1/ 与 root/s2/ 两个会话目录
    const fs::path root = "runs/webui_test";
    fs::remove_all(root);
    fs::create_directories(root / "s1");
    fs::create_directories(root / "s2");

    putFile(root / "s1" / "meta.json",
            R"({"session_id":"s1","step_count":2,"tool_count":3,"total_tokens":15,"start_ms":1000,"end_ms":1400})");
    putFile(root / "s1" / "run.json",
            R"({"id":"run-1","steps":[{"index":0,"model":"gpt-4o-mini","start_ms":1000,"end_ms":1180,"usage":{"total_tokens":15,"observed":true},"output_text":"hi","tools":[]}]})");
    putFile(root / "s1" / "graph.mmd", "flowchart TD\n    s0[\"x\"]\n");
    putFile(root / "s2" / "meta.json", "{ broken json");   // 坏文件容错

    // ═══ 1. 路径穿越防护（纯函数）═══════════════════════════════════
    std::string resolved, err;
    check(resolveRunDir(root.string(), "s1", resolved, err) && !resolved.empty(),
          "路径：合法 s1 通过", err);
    check(resolveRunDir(root.string(), "../..", resolved, err) == false,
          "安全：../.. 拒绝");
    check(resolveRunDir(root.string(), "/etc", resolved, err) == false,
          "安全：绝对路径拒绝");
    check(resolveRunDir(root.string(), "s1/nested", resolved, err) == false,
          "安全：多级路径拒绝（只允许直接子目录）");
    check(resolveRunDir(root.string(), "", resolved, err) == false,
          "安全：空 dir 拒绝");
    check(resolveRunDir(root.string(), "..", resolved, err) == false,
          "安全：单独 .. 拒绝");

    // ═══ 2. buildRunApiJson ══════════════════════════════════════════
    auto api = buildRunApiJson((root / "s1").string());
    check(api.value("ok", false) && api.value("dir", "") == "s1", "API：ok 与 dir");
    check(api.contains("meta") && api["meta"].value("step_count", 0) == 2,
          "API：meta 读入");
    check(api.value("mermaid", "").find("flowchart TD") != std::string::npos,
          "API：graph.mmd 原样带入");
    check(api.contains("steps") && api["steps"].is_array()
          && api["steps"].size() == 1, "API：steps 数组");
    check(api["steps"][0].value("model", "") == "gpt-4o-mini",
          "API：step 字段映射（model）");
    check(api["steps"][0].value("tokens", -1) == 15,
          "API：tokens 来自 total_tokens");

    auto badApi = buildRunApiJson((root / "s2").string());
    check(badApi.value("ok", false), "API：坏 meta.json 不拖垮整体（仍 ok）");
    check(badApi.contains("meta") && badApi["meta"].is_object(),
          "API：坏 meta 回落空对象");

    // ═══ 3. 真 HTTP 路由 ═════════════════════════════════════════════
    constexpr int kPort = 18081;
    httplib::Server svr;
    registerWebUi(svr, root.string(), kWebUiHtml);

    std::thread th([&] { svr.listen("127.0.0.1", kPort); });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    httplib::Client cli("127.0.0.1", kPort);
    cli.set_connection_timeout(2, 0);

    auto rHtml = cli.Get("/__xray");
    check(rHtml && rHtml->status == 200
          && rHtml->body.find("<html") != std::string::npos,
          "路由：GET /__xray 返回 HTML");
    check(rHtml && rHtml->get_header_value("Content-Type").find("charset=utf-8")
              != std::string::npos,
          "路由：HTML 带 charset=utf-8");

    auto rList = cli.Get("/__xray/api/runs");
    check(rList && rList->status == 200, "路由：GET /__xray/api/runs 200");
    if (rList && rList->status == 200)
    {
        auto j = nlohmann::json::parse(rList->body, nullptr, false);
        check(!j.is_discarded() && j.contains("runs") && j["runs"].is_array(),
              "路由：runs 列表是 JSON 数组");
    }

    auto rOk = cli.Get("/__xray/api/run?dir=s1");
    check(rOk && rOk->status == 200, "路由：dir=s1 返回 200");
    if (rOk && rOk->status == 200)
    {
        auto j = nlohmann::json::parse(rOk->body, nullptr, false);
        check(!j.is_discarded() && j.value("ok", false), "路由：API 响应 ok=true");
    }

    auto rBad = cli.Get("/__xray/api/run?dir=../..");
    check(rBad && rBad->status != 200, "安全：HTTP 层 dir=../.. 被拒（非 200）",
          rBad ? "status=" + std::to_string(rBad->status) : "no response");

    auto rNoDir = cli.Get("/__xray/api/run");
    check(rNoDir && rNoDir->status == 400, "路由：缺 dir 参数 400");

    svr.stop();
    th.join();
    fs::remove_all(root);

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
