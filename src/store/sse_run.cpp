#include "store/sse_run.h"

#include <utility>

#include "common/clock.h"

namespace mxr
{

    SseRunLogger::SseRunLogger(Recorder &rec, std::string hop)
        : rec_(rec), hop_(std::move(hop)), t0_(std::chrono::steady_clock::now())
    {
    }

    // 收到一块原始字节：喂在线累积器，并落一条 sse_chunk。
void SseRunLogger::onChunk(const std::string &rawBytes)
    {
        if (ended_)
            return;
        acc_.feed(rawBytes);
        Event ev;
        ev.type = "sse_chunk";
        ev.hop = hop_;
        ev.tsMs = nowMs();
        ev.body = rawBytes;
        ev.chunkIndex = index_++;
        rec_.push(std::move(ev));
    }

    // 流结束（或客户端断开）：落一条 sse_end 总结，幂等。
void SseRunLogger::onEnd()
    {
        if (ended_)
            return;
        ended_=true;
        acc_.finish();
        Event ev;
        ev.type = "sse_end";
        ev.hop = hop_;
        ev.tsMs = nowMs();
        ev.text = acc_.text();
        ev.badDataCount = acc_.badDataCount();
        ev.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0_).count();
        if(acc_.hasUsage()){
            SseUsage usage=acc_.usage();
            ev.promptTokens=usage.promptTokens;
            ev.completionTokens=usage.completionTokens;
            ev.totalTokens=usage.totalTokens;
        }
        rec_.push(std::move(ev));
    }

} // namespace mxr
