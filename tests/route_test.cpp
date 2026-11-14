// T3.1 验证台：路由匹配。纯字符串，不起网络。
//
// 跑法：./build/route_test.exe
// 期望：31 条全部 [ ok ]，末行 "31 passed, 0 failed"，退出码 0。
// 有一条失败就打印 [FAIL] + 实际值，退出码 1 —— 便于一眼看出是哪个判据写错了。
//
// 断言按三个函数分组：matchSegment（判边界）、stripPrefix（剥前缀）、
// resolve（找最长）+ validateUpstreams（启动校验）。

#include "proxy/route.h"
#include "proxy/target.h"

#include <iostream>
#include <string>
#include <vector>

using namespace mxr;

static int g_pass = 0;
static int g_fail = 0;

static void check(bool ok, const std::string &what, const std::string &detail = "")
{
    if (ok)
    {
        ++g_pass;
        std::cout << "[ ok ] " << what << "\n";
    }
    else
    {
        ++g_fail;
        std::cout << "[FAIL] " << what;
        if (!detail.empty())
        {
            std::cout << "   << " << detail;
        }
        std::cout << "\n";
    }
}

// 便于打印实际值：optional<string> → 可读字符串
static std::string show(const std::optional<std::string> &v)
{
    return v ? *v : "<nullopt>";
}
static std::string showPath(const std::optional<RouteResult> &r)
{
    return r ? r->path : "<nullopt>";
}

int main()
{
    // ── 1. matchSegment：段边界 ─────────────────────────────────
    check(matchSegment("/v1", "/v1/chat/completions"), "段边界：/v1 匹配 /v1/chat/completions");
    check(matchSegment("/v1", "/v1"), "段边界：/v1 匹配它自己");
    check(!matchSegment("/v1", "/v1beta/x"), "段边界：/v1 不匹配 /v1beta/x（'b' 不是边界）");
    check(!matchSegment("/v1", "/v10"), "段边界：/v1 不匹配 /v10");
    check(!matchSegment("/v1", "/v1x"), "段边界：/v1 不匹配 /v1x");
    check(matchSegment("/v1", "/v1?x=1"), "段边界：/v1 匹配 /v1?x=1（'?' 也是边界）");
    check(matchSegment("/", "/anything"), "段边界：/ 全匹配");
    check(!matchSegment("/v1/chat", "/v1"), "段边界：prefix 比 path 长 → false");

    // ── 2. stripPrefix：剥前缀 ──────────────────────────────────
    check(stripPrefix("/v1", "/v1/chat/completions") == "/chat/completions",
          "剥前缀：/v1 + /v1/chat/completions → /chat/completions",
          stripPrefix("/v1", "/v1/chat/completions"));
    check(stripPrefix("/v1", "/v1") == "/",
          "剥前缀：/v1 + /v1 → /（不能是空串）",
          "[" + stripPrefix("/v1", "/v1") + "]");
    check(stripPrefix("/", "/v1/chat") == "/v1/chat",
          "剥前缀：/ + /v1/chat → /v1/chat（补回斜杠）",
          stripPrefix("/", "/v1/chat"));
    check(stripPrefix("/v1", "/v1?x=1") == "/?x=1",
          "剥前缀：/v1 + /v1?x=1 → /?x=1（query 要跟着走）",
          stripPrefix("/v1", "/v1?x=1"));

    // ── 3. resolve：找最长 ─────────────────────────────────────
    std::vector<Upstream> one = {{"/v1", "http://127.0.0.1:9100/v1", "mock"}};

    auto r1 = resolve(one, "/v1/chat/completions");
    check(r1.has_value() && r1->path == "/chat/completions" && r1->upstream->name == "mock",
          "resolve：/v1/chat/completions → mock + /chat/completions",
          showPath(r1));

    auto r2 = resolve(one, "/v1");
    check(r2.has_value() && r2->path == "/", "resolve：/v1 本身 → /", showPath(r2));

    auto r3 = resolve(one, "/v1beta/x");
    check(!r3.has_value(), "resolve：/v1beta/x 无匹配 → nullopt（调用方回 404）", showPath(r3));

    auto r4 = resolve(one, "/v1/models?x=1");
    check(r4.has_value() && r4->path == "/models?x=1",
          "resolve：/v1/models?x=1 → /models?x=1", showPath(r4));

    std::vector<Upstream> two = {{"/v1", "http://a/v1", "short"},
                                 {"/v1/chat", "http://b", "long"}};
    auto r5 = resolve(two, "/v1/chat/completions");
    check(r5.has_value() && r5->upstream->name == "long" && r5->path == "/completions",
          "resolve：最长优先，/v1/chat 赢过 /v1", showPath(r5));

    // 同样的配置换个顺序，结果不能变（说明不是"先到先得"）
    std::vector<Upstream> two_rev = {{"/v1/chat", "http://b", "long"},
                                     {"/v1", "http://a/v1", "short"}};
    auto r6 = resolve(two_rev, "/v1/chat/completions");
    check(r6.has_value() && r6->upstream->name == "long",
          "resolve：结果与配置顺序无关", r6 ? r6->upstream->name : "<nullopt>");

    auto r7 = resolve(two, "/v1/models");
    check(r7.has_value() && r7->upstream->name == "short",
          "resolve：/v1/models 该归 /v1（不是所有请求都进 /v1/chat）",
          r7 ? r7->upstream->name : "<nullopt>");

    // ── 4. validateUpstreams：启动校验 ──────────────────────────
    check(!validateUpstreams(one).has_value(), "校验：合法配置 → nullopt", show(validateUpstreams(one)));

    check(validateUpstreams({{"api.openai.com", "http://x", "openai"}}).has_value(),
          "校验：match 不以 / 开头 → 报错",
          show(validateUpstreams({{"api.openai.com", "http://x", "openai"}})));

    check(validateUpstreams({{"/v1/", "http://x", "bad"}}).has_value(),
          "校验：match 以 / 结尾 → 报错",
          show(validateUpstreams({{"/v1/", "http://x", "bad"}})));

    check(validateUpstreams({{"/v1", "", "empty"}}).has_value(),
          "校验：url 为空 → 报错",
          show(validateUpstreams({{"/v1", "", "empty"}})));

    check(validateUpstreams({{"/v1", "http://a", "a"}, {"/v1", "http://b", "b"}}).has_value(),
          "校验：两条 match 重复 → 报错",
          show(validateUpstreams({{"/v1", "http://a", "a"}, {"/v1", "http://b", "b"}})));

    check(!validateUpstreams({{"/", "http://a", "root"}, {"/v1", "http://b", "v1"}}).has_value(),
          "校验：/ 和 /v1 共存 → 合法（长度不同，天然不冲突）",
          show(validateUpstreams({{"/", "http://a", "root"}, {"/v1", "http://b", "v1"}})));

    // ── 5. buildTarget：拆 url + 拼路径（T3.2）─────────────────
    auto t1 = buildTarget({"/v1", "http://127.0.0.1:9100/v1", "mock"}, "/chat/completions");
    check(t1 && t1->origin == "http://127.0.0.1:9100" && t1->path == "/v1/chat/completions",
          "目标：url 带 /v1，剥完的路径要拼回去",
          t1 ? t1->origin + " | " + t1->path : "<nullopt>");

    auto t2 = buildTarget({"/v1", "https://api.openai.com", "openai"}, "/v1/chat/completions");
    check(t2 && t2->origin == "https://api.openai.com" && t2->path == "/v1/chat/completions",
          "目标：url 没有路径部分 → path 原样",
          t2 ? t2->origin + " | " + t2->path : "<nullopt>");

    auto t3 = buildTarget({"/v1", "https://api.openai.com/", "openai"}, "/v1/chat");
    check(t3 && t3->path == "/v1/chat",
          "目标：url 尾部有 / → 不能拼出双斜杠",
          t3 ? t3->path : "<nullopt>");

    auto t4 = buildTarget({"/v1", "http://127.0.0.1:9100/v1", "mock"}, "/");
    check(t4 && t4->path == "/v1/",
          "目标：剥完是 / → /v1 + / = /v1/",
          t4 ? t4->path : "<nullopt>");

    auto t5 = buildTarget({"/v1", "127.0.0.1:9100", "bad"}, "/x");
    check(!t5, "目标：缺 scheme → nullopt");

    auto t6 = buildTarget({"/v1", "http:///v1", "bad"}, "/x");
    check(!t6, "目标：没有 host → nullopt");

    // ── 汇总 ──────────────────────────────────────────────────
    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
