#pragma once

#include <chrono>
#include <string>

#include "parse/sse.h"
#include "store/recorder.h"

namespace mxr {

// ═══════════════════════════════════════════════════════════════════
//  SseRunLogger —— 一条 SSE 流的"在线记录员"
// ═══════════════════════════════════════════════════════════════════
//
// 【它解决的是哪个矛盾】
//
//   转发要在**收到每一块的当下**就把字节交给客户端（T4.2 的边收边发），
//   而 M5 事后要能从文件里重建这同一条流。两件事的时间尺度完全不同：
//   一个按 200ms 一块，一个是进程退出后再读文件。
//
//   所以每条流配一个记录员：它站在 provider 旁边，每转发一块就记一笔，
//   流结束时记一笔总结。它不参与转发决策，不阻塞、不解析失败就报错。
//
// 【为什么单独一层，不直接塞进 server.cpp 的 lambda】
//
//   1. 状态要跨多次 provider 调用存活（累积器、块序号、开始时间、"是否收尾"），
//      塞进 lambda 就得用一堆 shared_ptr 捕获，读写都散落在 lambda 里。
//   2. 在线记录 和 离线 replay 必须用**同一个** SseAccumulator，
//      否则两边算法不同，对账就没意义。让记录员持有累积器，replay 也持有一个，
//      "同一份实现、喂两遍"这件事才成立。
//   3. 它只依赖 Recorder 和 SseAccumulator，不认识 httplib、不认识 socket ——
//      测试里可以不起服务器就喂它字节。
//
// 【线程与生命周期】
//
//   provider 在 httplib 的工作线程里被顺序调用，所以本类**不需要锁**。
//   用 shared_ptr 持有：httplib 会拷贝 std::function，handler 早就返回了，
//   provider 却可能还在跑；共享所有权保证它活到最后一次调用。
//
//   正常说完 → onEnd()；客户端断开 → 也要 onEnd()（记一条"不完整的总结"）。
//   onEnd 幂等：两条路径都可能触发，但 sse_end 只能有一条。
//
// 【为什么记"原始字节"而不是"解析后的文本"】
//
//   记结论等于让 replay 抄自己的答案 —— 解析器写错了两边一起错，对账全绿。
//   记原始字节，replay 必须真的重新跑一遍 T4.3 的状态机。
//   这也是自测题"进程在第 10 个 chunk 被杀，文件里有什么"的答案：
//   只有前 9 条 sse_chunk，没有 sse_end —— replay 会把它标成 incomplete。

class SseRunLogger {
public:
    // rec 必须活得比本对象久（Recorder 在 serve() 的栈上，满足）。
    // hop 把 sse_chunk / sse_end 和同一次请求的 req / resp 关联起来。
    SseRunLogger(Recorder &rec, std::string hop);

    // 收到一块**原始 SSE 字节**（可能半行）。先喂累积器，再落一条 sse_chunk。
    void onChunk(const std::string &rawBytes);

    // 流结束（正常或客户端断开）：flush 累积器 → 落一条 sse_end。幂等。
    void onEnd();

private:
    Recorder &rec_;
    std::string hop_;
    SseAccumulator acc_;
    long long index_ = 0;       // 流内块序号，和全局 seq 分开
    bool ended_ = false;        // 幂等守卫：sse_end 只发一次
    std::chrono::steady_clock::time_point t0_;
};

} // namespace mxr
