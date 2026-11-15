#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>

namespace mxr {

// ═══════════════════════════════════════════════════════════════════
//  StreamBridge —— 在两个线程之间搬运一条字节流
// ═══════════════════════════════════════════════════════════════════
//
// 【它解决的是哪个矛盾】
//
//   上游：httplib::Client::send() 是**同步**的 —— 它一直阻塞到整个 body
//         收完才返回。SSE 上游说 20 秒，它就阻塞 20 秒。
//
//   下游：httplib::Server 是**回填**的 —— handler 必须先把 res 填好、立刻返回，
//         库才开始往客户端写。handler 卡住 = 客户端一个字节都收不到。
//
// 一个"必须阻塞到收完"，一个"必须立刻返回"，同一个线程里做不到。
// 所以把 send() 挪到单独线程，本类就是这两个线程之间的那座桥：
//
//   上游线程 ──push()──>  [有界队列]  ──next()──> httplib 工作线程
//     （生产者）           <= 1 MiB              （消费者 / provider）
//
// 【为什么单独一层】
//
// 它不认识 HTTP、不认识 SSE、不认识 JSON —— 只知道"字节进来、字节出去"。
// T4.3 的 SSE 解析器、T4.4 的落盘都挂在**同一股字节流**上，各拿各的，
// 谁也不用知道对方存在。这也是任务书里说的 sink 抽象的最小形态。
//
// 【和 Recorder 的队列的关键差别：满了不许丢】
//
//   Recorder（事件）：满了 → 丢掉 + 计数。丢一条事件，链路还原少一条，无伤大雅。
//   StreamBridge（字节）：满了 → **生产者阻塞等待**。丢一个字节，下游的
//                        `data: {json}` 就残了，整段回答作废 —— 这不是"少一点
//                        信息"，是"结果是错的"。
//
//   阻塞生产者带来的副作用正好是我们想要的：上游线程卡住 → 它不再从 socket 读
//   → 上游的 TCP 发送窗口被填满 → 上游那边的写也慢下来。这就是**背压**。
//   整条链的速率自动对齐到最慢的一环（这里是客户端），不需要任何限速代码。
//
//   为什么必须有上界：客户端断网但 TCP 还没超时的那几十秒里，上游还在拼命吐。
//   无限队列 = 这几秒能把内存吃光。有界 = 最多 1 MiB，然后大家一起等。
//
// 【生命周期】
//
//   用 shared_ptr 在两个线程间共享。谁最后放手谁销毁：
//     上游线程持有 → 它跑完 send() 才释放
//     provider 持有 → 它拿不到数据时才释放
//   谁先走都不会把对方要用的对象拆掉。
//
// 【状态机】
//
//                 setHead(status, ct)
//   (初始) ─────────────────────────> 头已就绪 ──> 可以有数据了
//      │                                   │
//      │ fail(why)                         │ finish()  ← 上游正常说完
//      ▼                                   ▼
//   失败（closed）                     结束（closed）
//                                          │
//                     abort()  ← 客户端断开，随时可能从任何状态跳过来
//                        ▼
//                     结束（closed + aborted）
//
//   closed_ 一旦为 true：push 立刻返回 false（告诉上游"别发了"），
//   next 在队列排空后返回 false（告诉 provider"说完了"）。

class StreamBridge {
public:
    // 队列能攒的字节上限。选 1 MiB 的理由：SSE 单块通常 < 4 KiB，
    // 这个量级足够吸收网络抖动，又不至于让"客户端卡死"变成内存问题。
    static constexpr std::size_t kMaxQueuedBytes = 1u << 20;

    // ── 上游线程（生产者）─────────────────────────────────────────

    // 响应头到了：记下状态码和 Content-Type，唤醒 waitHead 的下游。
    // 只会生效一次（第一次调用为准）。
    void setHead(int status, std::string contentType);

    // 塞一块字节。队列满了会**阻塞**（背压），closed 之后返回 false。
    // 返回 false 的意思是"下游不要了，你别再发了" —— content_receiver 直接
    // return 这个值，send() 就会中断。
    bool push(const char *data, std::size_t len);

    // 上游正常说完（send 成功返回）：置 closed，唤醒所有等待者。
    void finish();

    // 上游出错（连不上、读超时）：记下原因，置 closed + failed，唤醒所有人。
    void fail(std::string why);

    // ── 下游线程（消费者 / provider）───────────────────────────────

    // 等响应头，最多等 timeoutMs 毫秒。
    //   返回 true  → 头到了（可能同时已经是 failed 状态，要再看 failed()）
    //   返回 false → 超时，上游连头都没给（连接卡住 / 端口不通）
    bool waitHead(int timeoutMs);

    // 取下一块字节：
    //   返回 true  → out 里是数据
    //   返回 false → 流结束（closed 且队列已排空）—— 此后不会再有数据
    // 队列空但流没结束时**阻塞等待**，这是"边收边发"能成立的地方。
    bool next(std::string &out);

    // 客户端断开：置 closed + aborted，唤醒所有人。
    // 效果是让卡在 push 里的上游线程立刻返回 false，从而中断 send()。
    void abort();

    // ── 查询 ────────────────────────────────────────────────────
    int status() const;
    std::string contentType() const;
    std::string error() const;
    bool failed() const;
    bool aborted() const;
    std::size_t queuedBytes() const;

private:
    // 置结束标志。调用者必须已经持有 mu_（所以名字带 Locked）。
    void closeLocked(bool aborted);

    mutable std::mutex mu_;
    std::condition_variable cvData_;  // 数据可读 / 流结束
    std::condition_variable cvSpace_; // 队列有空间 / 流结束
    std::condition_variable cvHead_;  // 头就绪 / 流结束

    std::deque<std::string> q_;    // 待发出的块，按到达顺序
    std::size_t queued_ = 0;       // q_ 里的字节总数（不是块数）

    bool closed_ = false;
    bool aborted_ = false;
    bool failed_ = false;
    bool headReady_ = false;

    int status_ = 0;
    std::string ct_;
    std::string err_;
};

} // namespace mxr
