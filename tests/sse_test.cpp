// T4.3 验证台：SseAccumulator（纯字符串操作，不联网、不起进程）
//
// 跑法：./build/sse_test.exe
// 期望：全部 [ ok ]，末行 "N passed, 0 failed"，退出码 0。
//
// 验收的三条硬指标（任务书原文）都在这里：
//   1. 1 字节切片喂入 == 整块喂入（测试 2/3）
//   2. delta.content 累积 == 完整文本（测试 1）
//   3. 坏块不影响后续解析（测试 6）

#include "parse/sse.h"

#include <iostream>
#include <string>

using mxr::SseAccumulator;
using mxr::SseUsage;

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

// 拼一份和 mock 上游逐字一致的 SSE 流（10 块 content + 1 块 stop/usage + [DONE]）
static std::string makeMockStream(const std::string &newline = "\n")
{
    auto chunk = [](const std::string &content, const std::string &finish,
                    const std::string &usage) {
        std::string s = "{\"choices\":[{\"delta\":";
        if (content.empty()) {
            s += "{}";
        } else {
            s += "{\"content\":\"" + content + "\"}";
        }
        s += ",\"finish_reason\":" + finish + ",\"index\":0}],"
             "\"id\":\"chatcmpl-mock\",\"model\":\"m\",\"object\":\"chat.completion.chunk\"";
        if (!usage.empty()) {
            s += ",\"usage\":" + usage;
        }
        s += "}"; // 关顶层对象（usage 也是顶层字段，必须在关括号之前追加）
        return s;
    };

    std::string stream;
    for (int i = 0; i < 10; ++i) {
        stream += "data: " + chunk("tok" + std::to_string(i) + " ", "null", "") + newline + newline;
    }
    stream += "data: " + chunk("", "\"stop\"", "{\"prompt_tokens\":10,\"completion_tokens\":10,\"total_tokens\":20}")
              + newline + newline;
    stream += "data: [DONE]" + newline + newline;
    return stream;
}

static const std::string kExpectedText = "tok0 tok1 tok2 tok3 tok4 tok5 tok6 tok7 tok8 tok9 ";

// 对一份"喂完完整 mock 流"的累积器做全套断言
static void checkFullStream(const SseAccumulator &acc, const std::string &tag)
{
    check(acc.done(), tag + ": [DONE] 被认出");
    check(acc.text() == kExpectedText, tag + ": 累积文本完整", "[" + acc.text() + "]");
    check(acc.eventCount() == 11, tag + ": 事件数 11",
          std::to_string(acc.eventCount()));
    check(acc.badDataCount() == 0, tag + ": 没有坏块");
    check(acc.finishReason() == "stop", tag + ": finish_reason == stop",
          "[" + acc.finishReason() + "]");
    check(acc.hasUsage(), tag + ": usage 被提取");
    const SseUsage &u = acc.usage();
    check(u.promptTokens == 10 && u.completionTokens == 10 && u.totalTokens == 20,
          tag + ": usage 三数正确",
          std::to_string(u.promptTokens) + "/" + std::to_string(u.completionTokens)
              + "/" + std::to_string(u.totalTokens));
}

int main()
{
    // ── 1. 整块喂入 ────────────────────────────────────────────
    {
        SseAccumulator acc;
        acc.feed(makeMockStream());
        acc.finish();
        checkFullStream(acc, "整块");
    }

    // ── 2. 1 字节切片喂入（流式解析的核心验收）──────────────────
    {
        SseAccumulator acc;
        const std::string s = makeMockStream();
        for (char c : s) {
            acc.feed(std::string(1, c));
        }
        acc.finish();
        checkFullStream(acc, "1字节");
    }

    // ── 3. 7 字节不等切片（切点落在 data: 中间、JSON 中间、\n 中间）──
    {
        SseAccumulator acc;
        const std::string s = makeMockStream();
        for (std::size_t i = 0; i < s.size(); i += 7) {
            acc.feed(s.substr(i, 7));
        }
        acc.finish();
        checkFullStream(acc, "7字节");
    }

    // ── 4. \r\n 行尾（curl / Windows 服务端的习惯）──────────────
    {
        SseAccumulator acc;
        acc.feed(makeMockStream("\r\n"));
        acc.finish();
        checkFullStream(acc, "CRLF");
    }

    // ── 5. 注释行 + event/id 行：忽略，不影响结果 ────────────────
    {
        SseAccumulator acc;
        acc.feed(": keep-alive\n\n");
        acc.feed("event: message\ndata: {\"choices\":[{\"delta\":{\"content\":\"hi \"}}]}\n\n");
        acc.feed(": ping\ndata: {\"choices\":[{\"delta\":{\"content\":\"there\"}}]}\n\n");
        acc.finish();
        check(acc.text() == "hi there", "杂行：注释与 event 行被忽略", "[" + acc.text() + "]");
        check(acc.eventCount() == 2, "杂行：事件数 2", std::to_string(acc.eventCount()));
        check(acc.badDataCount() == 0, "杂行：没有坏块");
    }

    // ── 6. 坏块混在中间：计数跳过，后续照常 ──────────────────────
    {
        SseAccumulator acc;
        const std::string s = makeMockStream();
        // 插在第 6 个 data 行的**行首**（不能插在 JSON 字符串中间，否则那一块本身被切碎）
        const std::size_t cut = s.rfind("data: ", s.find("tok5"));
        std::string broken = s.substr(0, cut) + "data: {bad json}\n\n" + s.substr(cut);
        acc.feed(broken);
        acc.finish();
        check(acc.badDataCount() == 1, "坏块：计数 1", std::to_string(acc.badDataCount()));
        check(acc.text() == kExpectedText, "坏块：后续文本不受影响");
        check(acc.eventCount() == 11, "坏块：好事件照常计数", std::to_string(acc.eventCount()));
        check(acc.done(), "坏块：[DONE] 照常生效");
    }

    // ── 7. 多行 data 拼接（规范：两行 data 用 \n 连成一个事件）────
    //   拼接后 {"choices":[\n{"delta":{"content":"X"}}]} 是**合法** JSON
    //   （token 之间允许换行空白）→ text_ 正好拿到 "X"、事件数 1。
    //   如果实现错了、把两行各自分发，两半都是非法 JSON → badDataCount == 2。
    {
        SseAccumulator acc;
        acc.feed("data: {\"choices\":[\ndata: {\"delta\":{\"content\":\"X\"}}]}\n\n");
        acc.finish();
        check(acc.text() == "X" && acc.eventCount() == 1 && acc.badDataCount() == 0,
              "多行 data：两行拼成一个事件（而非两次分发）",
              "text=[" + acc.text() + "] bad=" + std::to_string(acc.badDataCount())
                  + " events=" + std::to_string(acc.eventCount()));
    }

    // ── 8. finish 收尾：残留半行 / 没等到空行的事件 ──────────────
    {
        // a. 最后一行没有 \n 就断了连接
        SseAccumulator acc;
        acc.feed("data: {\"choices\":[{\"delta\":{\"content\":\"tail\"}}]}");
        acc.finish();
        check(acc.text() == "tail", "finish：无换行的最后半行被处理", "[" + acc.text() + "]");
    }
    {
        // b. [DONE] 后面忘了跟空行
        SseAccumulator acc;
        acc.feed(makeMockStream());
        acc.feed("data: [DONE]"); // 没有 \n\n
        acc.finish();
        check(acc.done(), "finish：缺空行的 [DONE] 仍生效");
    }

    // ── 9. 空流 / 只有注释：不产出、不报坏块 ─────────────────────
    {
        SseAccumulator acc;
        acc.feed("");
        acc.feed(": heartbeat\n");
        acc.finish();
        check(acc.eventCount() == 0 && acc.badDataCount() == 0 && !acc.done()
                  && acc.text().empty(),
              "空流：无事件、无坏块、未 done");
    }

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
