// T4.2 验证台：StreamBridge（纯线程，不联网、不起进程）
//
// 跑法：./build/stream_bridge_test.exe
// 期望：全部 [ ok ]，末行 "N passed, 0 failed"，退出码 0。
//
// 这一关的断言都是"时序"性质，所以每条都要有超时兜底 ——
// 桥写错了的典型症状是**永久挂住**（生产者等空间，消费者等数据），
// 没有超时的话测试会停在那里，看不出是哪一条。

#include "proxy/stream_bridge.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace mxr;
using namespace std::chrono_literals;

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

// 在后台跑一个可能永久阻塞的动作，最多等 ms 毫秒。
// 返回 true = 它在时限内结束了；false = 还卡着（大概率是 bug）。
template <typename F>
static bool runWithTimeout(F f, int ms)
{
    std::atomic<bool> done{false};
    std::thread t([&] { f(); done = true; });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    if (done) {
        t.join();
        return true;
    }
    t.detach(); // 卡住了：放弃这条线程，让进程继续（后面会判 FAIL）
    return false;
}

int main()
{
    // ── 1. 头：setHead → waitHead ─────────────────────────────
    {
        StreamBridge b;
        b.setHead(200, "text/event-stream");
        check(b.waitHead(1000), "头：设置后立刻可等");
        check(b.status() == 200, "头：状态码记住了");
        check(b.contentType() == "text/event-stream", "头：Content-Type 记住了");
        check(!b.failed(), "头：正常设置不算失败");
    }

    // ── 2. 没头就等，要有超时 ──────────────────────────────────
    {
        StreamBridge b;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = b.waitHead(150);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        check(!ok, "超时：没设头时 waitHead 返回 false");
        check(ms >= 100 && ms < 1500, "超时：确实等满了时限", std::to_string(ms) + "ms");
    }

    // ── 3. 失败：waitHead 不该干等到超时 ────────────────────────
    {
        StreamBridge b;
        b.fail("Connection");
        check(b.waitHead(1000), "失败：waitHead 立刻返回 true（不是超时）");
        check(b.failed(), "失败：failed() 为真");
        check(b.error() == "Connection", "失败：原因记住了");
        std::string s;
        check(!b.next(s), "失败：next 返回 false");
    }

    // ── 4. 顺序 + 收尾 ────────────────────────────────────────
    {
        StreamBridge b;
        b.push("aa", 2);
        b.push("bbb", 3);
        b.push("c", 1);
        std::string s;
        const bool a = b.next(s);
        const std::string s1 = s;
        b.next(s);
        const std::string s2 = s;
        b.next(s);
        const std::string s3 = s;
        b.finish();
        const bool end = b.next(s);
        check(a && s1 == "aa" && s2 == "bbb" && s3 == "c",
              "顺序：三块按入队顺序出来", s1 + "|" + s2 + "|" + s3);
        check(!end, "收尾：finish 且排空后 next 返回 false");
        check(b.next(s) == false, "收尾：之后永远是 false");
    }

    // ── 5. next 会阻塞等到数据来（边收边发的核心）────────────────
    {
        StreamBridge b;
        std::thread producer([&] {
            std::this_thread::sleep_for(80ms);
            b.push("late", 4);
            b.finish();
        });
        const auto t0 = std::chrono::steady_clock::now();
        std::string s;
        const bool got = b.next(s);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        producer.join();
        check(got && s == "late", "阻塞：next 等到了后来才到的数据", "[" + s + "]");
        check(ms >= 50, "阻塞：确实等了一会儿（不是空转）", std::to_string(ms) + "ms");
    }

    // ── 6. 背压：队列满了 push 必须等，不能丢 ────────────────────
    {
        StreamBridge b;
        const std::size_t block = 64 * 1024;
        std::string chunk(block, 'x');

        // 先塞满到上限附近（不移除的话第 17 块会被卡住，所以这里放进线程）
        std::atomic<bool> pushDone{false};
        std::atomic<bool> pushRet{true};
        std::thread producer([&] {
            for (int i = 0; i < 32; ++i) { // 32 * 64KiB = 2 MiB > 上限 1 MiB
                if (!b.push(chunk.data(), chunk.size())) {
                    pushRet = false;
                    break;
                }
            }
            pushDone = true;
        });

        std::this_thread::sleep_for(120ms);
        const bool blocked = !pushDone.load(); // 应该卡住
        const std::size_t peak = b.queuedBytes();
        check(blocked, "背压：超过上限后 push 阻塞（没有丢数据）");
        check(peak <= StreamBridge::kMaxQueuedBytes, "背压：队列字节数没超过上限",
              std::to_string(peak));

        // 消费掉，生产者应该继续推进并最终完成
        std::string s;
        std::size_t drained = 0;
        while (drained < 32 * block) {
            if (!b.next(s)) break;
            drained += s.size();
        }
        const bool finished = runWithTimeout([&] { producer.join(); }, 2000);
        check(finished && pushDone.load(), "背压：消费者排空后生产者继续跑完");
        check(pushRet.load(), "背压：push 全程没有返回 false（没被当成下游关闭）");
    }

    // ── 7. abort：卡在 push 上的生产者必须被叫醒 ────────────────
    {
        StreamBridge b;
        std::string chunk(64 * 1024, 'y');
        std::atomic<int> ret{-1};
        std::thread producer([&] {
            int i = 0;
            for (; i < 64; ++i) {
                if (!b.push(chunk.data(), chunk.size())) {
                    ret = 0;
                    return;
                }
            }
            ret = 1; // 一路畅通（不该发生）
        });

        std::this_thread::sleep_for(100ms);
        check(ret.load() == -1, "abort：生产者此刻卡在 push 上（前提成立）");
        b.abort();
        const bool woke = runWithTimeout([&] { producer.join(); }, 2000);
        check(woke, "abort：生产者被唤醒并退出");
        check(ret.load() == 0, "abort：push 返回了 false");
        check(b.aborted(), "abort：aborted() 为真");
    }

    // ── 8. abort 后：队列里已有的数据仍可取，取完就结束 ──────────
    {
        StreamBridge b;
        b.push("zzz", 3);
        b.abort();
        std::string s;
        // 队列里那块还取得到（已经收下的数据没必要扔），但取完就结束
        const bool first = b.next(s);
        const bool second = b.next(s);
        check(first && s == "zzz", "abort：队列里已有的数据仍可取");
        check(!second, "abort：取完立刻返回 false");
    }

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
