// T4.1 验证台：chunked 解码。纯字节操作，不起网络。
//
// 跑法：./build/chunked_test.exe
// 期望：全部 [ ok ]，末行 "N passed, 0 failed"，退出码 0。
//
// 核心断言是"**任意切法结果一致**"：同一段原始流按 1 / 2 / 3 / 5 / 7 / 13 字节
// 喂进去，解出来的 payload 必须逐字节相同。这条覆盖了"CRLF 被劈开"、
// "长度行被劈开"、"数据被劈开"三种情况 —— 因为它们都只是切片点落在不同位置。

#include "parse/chunked.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace mxr;

static int g_pass = 0;
static int g_fail = 0;

static void check(bool ok, const std::string& what, const std::string& detail = "")
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

// 把 raw 按 step 字节切片，依次 feed，拼起来返回。
struct Result {
    std::string out;
    bool done = false;
    bool failed = false;
};

static Result decodeSlices(std::string_view raw, std::size_t step)
{
    ChunkedDecoder dec;
    Result r;
    for (std::size_t i = 0; i < raw.size(); i += step) {
        const std::size_t n = std::min(step, raw.size() - i);
        r.out += dec.feed(raw.substr(i, n));
    }
    r.done = dec.done();
    r.failed = dec.failed();
    return r;
}

// 可打印化：把不可见字节换成 \r \n 之类的转义，方便看失败原因
static std::string esc(std::string_view s)
{
    std::string out;
    for (char c : s) {
        switch (c) {
        case '\r': out += "\\r"; break;
        case '\n': out += "\\n"; break;
        default:   out += c;
        }
    }
    return out;
}

// 一段"标准"的流：Hello + 27 字节 JSON 片段 + 结束
//   5\r\nHello\r\n  1b\r\n{"delta":{"content":"abc"}}\r\n  0\r\n\r\n
// （0x1b = 27：{\"delta\":{\"content\":\"abc\"}} 数一遍就是 27 个字节）
static const std::string kRaw =
    "5\r\nHello\r\n"
    "1b\r\n{\"delta\":{\"content\":\"abc\"}}\r\n"
    "0\r\n\r\n";

static const std::string kExpect =
    "Hello"
    "{\"delta\":{\"content\":\"abc\"}}";

int main()
{
    // ── 1. 整块喂入 ───────────────────────────────────────────
    {
        ChunkedDecoder dec;
        const std::string out = dec.feed(kRaw);
        check(out == kExpect, "整块：payload 正确", "得到 [" + esc(out) + "]");
        check(dec.done(), "整块：done() 为真");
        check(!dec.failed(), "整块：没有失败");
        check(dec.pending() == 0, "整块：没有残留字节");
    }

    // ── 2. 任意切法结果一致 ────────────────────────────────────
    for (std::size_t step : {std::size_t(1), std::size_t(2), std::size_t(3),
                             std::size_t(5), std::size_t(7), std::size_t(13)}) {
        const Result r = decodeSlices(kRaw, step);
        check(r.out == kExpect, "按 " + std::to_string(step) + " 字节切片：payload 一致",
              "得到 [" + esc(r.out) + "]");
        check(r.done, "按 " + std::to_string(step) + " 字节切片：done() 为真");
        check(!r.failed, "按 " + std::to_string(step) + " 字节切片：没有失败");
    }

    // ── 3. 长度行被劈开（\r 和 \n 分两次到）─────────────────────
    {
        ChunkedDecoder dec;
        check(dec.feed("5\r").empty(), "CRLF 劈开：只有 \\r 时不出数据");
        check(!dec.feed("\nHel").empty(), "CRLF 劈开：\\n 补齐后立刻出数据");

        ChunkedDecoder dec2;
        dec2.feed("5\r");
        const std::string a = dec2.feed("\nHel");
        const std::string b = dec2.feed("lo\r\n0\r\n\r\n");
        check(a + b == "Hello", "CRLF 劈开：拼起来是 Hello", "得到 [" + esc(a + b) + "]");
        check(dec2.done(), "CRLF 劈开：最终 done");
    }

    // ── 4. 见到 0 但不能立刻 done（trailer 还没读完）─────────────
    {
        ChunkedDecoder dec;
        const std::string a = dec.feed("5\r\nHello\r\n0");
        check(a == "Hello", "尾部：0 之前的数据照常吐出");
        check(!dec.done(), "尾部：只收到 \"0\" 时还不能 done");
        const std::string b = dec.feed("\r\n");
        check(b.empty(), "尾部：补齐 \"0\" 的长度行 CRLF，仍不产生数据");
        check(!dec.done(), "尾部：trailer 的收尾 CRLF 还没到，仍然不能 done");
        const std::string c = dec.feed("\r\n");
        check(c.empty(), "尾部：最后那个 CRLF 不产生数据");
        check(dec.done(), "尾部：读完空行才 done");
    }

    // ── 5. chunk extension ───────────────────────────────────
    {
        ChunkedDecoder dec;
        const std::string out = dec.feed("5;name=value\r\nHello\r\n0\r\n\r\n");
        check(out == "Hello", "extension：分号后面被丢弃", "得到 [" + esc(out) + "]");
        check(dec.done(), "extension：done");
    }

    // ── 6. trailer header ────────────────────────────────────
    {
        ChunkedDecoder dec;
        const std::string out =
            dec.feed("5\r\nHello\r\n0\r\nX-Checksum: abc\r\nX-Other: 1\r\n\r\n");
        check(out == "Hello", "trailer：数据正确", "得到 [" + esc(out) + "]");
        check(dec.done(), "trailer：跳过多行 trailer 后 done");
        check(dec.pending() == 0, "trailer：没有残留字节");
    }

    // ── 7. 大写十六进制 ───────────────────────────────────────
    {
        ChunkedDecoder dec;
        const std::string out = dec.feed("A\r\n0123456789\r\n0\r\n\r\n");
        check(out == "0123456789", "大写 A：解析成 10", "得到 [" + esc(out) + "]");
    }

    // ── 8. 数据里含 \r\n\r\n（证明不能按分隔符切）──────────────
    {
        ChunkedDecoder dec;
        // payload 是 4 个字节：\r\n\r\n
        // 拼出来是 4\r\n + \r\n\r\n + \r\n + 0\r\n\r\n（用字符串拼，避免数 \r\n 数错）
        const std::string raw = "4\r\n" + std::string("\r\n\r\n") + "\r\n0\r\n\r\n";
        const std::string out = dec.feed(raw);
        check(out == "\r\n\r\n", "数据含 CRLFCRLF：完整保留，没有被当成边界",
              "得到 [" + esc(out) + "]");
        check(dec.done(), "数据含 CRLFCRLF：done");
    }

    // ── 9. 非法长度行 ────────────────────────────────────────
    {
        ChunkedDecoder dec;
        dec.feed("zz\r\nHello\r\n0\r\n\r\n");
        check(dec.failed(), "非法长度：failed() 为真");
        check(!dec.done(), "非法长度：不是 done");
        check(dec.feed("more").empty(), "非法长度：之后 feed 返回空");
    }

    // ── 10. 空长度行 ─────────────────────────────────────────
    {
        ChunkedDecoder dec;
        dec.feed("\r\nHello\r\n0\r\n\r\n");
        check(dec.failed(), "空长度行：failed() 为真");
    }

    // ── 11. 数据后面的 CRLF 不对 ──────────────────────────────
    {
        ChunkedDecoder dec;
        dec.feed("5\r\nHelloXX0\r\n\r\n");
        check(dec.failed(), "块尾不是 CRLF：failed() 为真");
    }

    // ── 12. 空 feed ──────────────────────────────────────────
    {
        ChunkedDecoder dec;
        check(dec.feed("").empty(), "空 feed：返回空");
        check(!dec.done() && !dec.failed(), "空 feed：状态不变");
    }

    // ── 13. done 之后再 feed ─────────────────────────────────
    {
        ChunkedDecoder dec;
        dec.feed("5\r\nHello\r\n0\r\n\r\n");
        check(dec.done(), "完成后：done");
        check(dec.feed("garbage").empty(), "完成后：再 feed 返回空串");
        check(dec.done(), "完成后：仍是 done");
    }

    // ── 14. 半截数据停在中间 ─────────────────────────────────
    {
        ChunkedDecoder dec;
        const std::string a = dec.feed("1a\r\nabc");
        check(a == "abc", "半截数据：已经到的 3 个字节先吐出来", "得到 [" + esc(a) + "]");
        check(dec.pending() == 0, "半截数据：没有残留（都吐出去了）");
        check(!dec.done(), "半截数据：还没完成");
    }

    // ── 15. 大块（4KB）分片 ──────────────────────────────────
    {
        std::string payload;
        for (int i = 0; i < 4096; ++i) payload += static_cast<char>('a' + (i % 26));

        // 用字符串拼出合法的 chunked 文本
        std::string raw = "1000\r\n" + payload + "\r\n0\r\n\r\n";

        const Result r = decodeSlices(raw, 777);
        check(r.out == payload, "4KB 块按 777 切片：内容一致");
        check(r.done, "4KB 块按 777 切片：done");
    }

    // ── 汇总 ──────────────────────────────────────────────────
    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
