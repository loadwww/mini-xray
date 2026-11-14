#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "store/event.h"
#include "store/jsonl_writer.h"

namespace mxr {

// 异步记录器：handler 只管把事件塞进队列，真正的脱敏 + 序列化 + 写盘
// 全在一条独立的写线程上做。
//
// 为什么必须异步：
//   写盘是"不可预测的慢"——磁盘忙、杀毒软件扫文件、网络盘掉线，任何一次
//   write 都可能卡几百毫秒。而这个 write 如果发生在 handler 里，被卡住的是
//   **转发线程**，等于把上游的响应时间凭空拉长。代理是流量必经之路，它慢
//   1ms 就是所有请求慢 1ms。
//   所以：handler 只做内存操作（快、可预测），把"I/O 的不确定性"甩给写线程。
//
// 为什么队列要有上限：
//   上游一直来、磁盘一直慢，队列就会无限涨 → 内存被吃光 → 进程被杀。
//   有上限 + "满了就丢弃并计数"，是一种**受控的降级**：宁可少记几条事件，
//   也不能让代理因为"记日志"而 OOM。丢了多少由 dropped() 报出来。
//
// 线程模型：
//   多个 handler 线程 ──push()──> [有界队列] ──run()──> 写线程 ──> events.jsonl
//                    （加锁 + notify）        （wait + 取 + 锁外写）
//
// 生命周期：
//   构造 = 打开文件 + 起写线程；析构 = 通知收工 + 等它把队列写空 + join。
//   **必须在 serve() 的栈上创建**，这样进程正常退出时析构一定会跑。
//   （Ctrl+C 强制杀进程的话析构不会跑 —— 所以 server.cpp 里装了 console 处理器，
//     让 Ctrl+C 变成"停下来、正常 return"，而不是直接终止进程。）
class Recorder {
public:
    explicit Recorder(const std::string& path);   // 打开失败 → 抛 std::runtime_error
    ~Recorder();

    Recorder(const Recorder&) = delete;            // 持有 mutex + thread，天然不可拷贝
    Recorder& operator=(const Recorder&) = delete;

    // 任何线程都能调；保证不阻塞、不抛异常（写盘失败只计数，见 run()）
    void push(Event ev);

    // 收工：置停止标志 → 叫醒写线程 → 等它把队列写空 → join。
    // 可以重复调用（第二次没有线程可 join，直接返回）。
    // 析构就是调它；单独公开是因为 serve() 想在退出时打印"写了多少、丢了多少"，
    // 那时候必须先把队列排空，数字才有意义。
    void flush();

    long long dropped() const { return dropped_.load(); }
    long long written() const { return written_.load(); }

private:
    void run();                                    // 写线程主循环

    // 上限怎么定：一条事件平均几百字节到几 KB，4096 条大概几 MB ——
    // 足够吸收"磁盘卡 1 秒"这种抖动，又不至于把内存吃掉。
    static constexpr std::size_t kMaxQueued = 4096;

    JsonlWriter writer_;              // 必须第一个声明：写线程一起来就要用它
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::queue<Event> q_;
    bool stopping_ = false;
    long long seq_ = 0;               // 只在锁内读写
    std::thread th_;
    std::atomic<long long> dropped_{0};
    std::atomic<long long> written_{0};
};

} // namespace mxr
