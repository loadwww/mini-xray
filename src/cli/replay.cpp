#include "cli/replay.h"

#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "parse/sse.h"

namespace mxr
{

    // 逐行重放事件文件：把 sse_chunk 的原始字节喂进新建的累积器，与 sse_end 对账。
ReplaySummary replayFile(const std::string &path, std::ostream &os)
    {
        ReplaySummary sum;
        std::ifstream in(path);
        if (!in)
        {
            return sum; // opened 仍是 false
        }
        sum.opened = true;

        SseAccumulator acc;
        std::string curHop;
        long long expectedIndex = 0;
        bool streamOpen = false;
        long long streamChunks = 0;

        std::string line;
        while (std::getline(in, line))
        {
            auto j = nlohmann::json::parse(line, nullptr, false);
            if (j.is_discarded()) {
                continue;
            }
            const std::string type = j.value("type", "");

            if (type == "sse_chunk") {
                const std::string hop = j.value("hop", "");
                if (!streamOpen || hop != curHop) {
                    if (streamOpen) {
                        ++sum.incomplete;
                        os << "  [" << curHop << "] 不完整：换流时仍缺 sse_end（"
                           << streamChunks << " 块）\n";
                    }
                    acc = SseAccumulator();
                    expectedIndex = 0;
                    streamChunks = 0;
                    curHop = hop;
                    streamOpen = true;
                }
                const long long idx = j.value("chunk_index", -1LL);
                if (idx != expectedIndex) {
                    ++sum.mismatches;
                    os << "  [" << hop << "] chunk_index 异常：期望 " << expectedIndex
                       << "，实际 " << idx << "\n";
                }
                acc.feed(j.value("body", ""));
                ++sum.chunks;
                ++streamChunks;
                ++expectedIndex;
            } else if (type == "sse_end") {
                acc.finish();
                const std::string saved = j.value("text", "");
                const std::string rebuilt = acc.text();

                long long prompt = 0, completion = 0, total = 0;
                if (j.contains("usage")) {
                    const auto &u = j["usage"];
                    prompt = u.value("prompt_tokens", 0LL);
                    completion = u.value("completion_tokens", 0LL);
                    total = u.value("total_tokens", 0LL);
                }
                const SseUsage &ru = acc.usage();

                const bool textOk = (saved == rebuilt);
                const bool usageOk = (prompt == ru.promptTokens
                                      && completion == ru.completionTokens
                                      && total == ru.totalTokens);
                if (!textOk || !usageOk) {
                    ++sum.mismatches;
                    os << "  [" << curHop << "] 不一致：\n";
                    if (!textOk) {
                        os << "    text  落盘=[" << saved << "] 重建=[" << rebuilt << "]\n";
                    }
                    if (!usageOk) {
                        os << "    usage 落盘=" << prompt << "/" << completion << "/" << total
                           << " 重建=" << ru.promptTokens << "/" << ru.completionTokens
                           << "/" << ru.totalTokens << "\n";
                    }
                } else {
                    os << "  [" << curHop << "] " << streamChunks
                       << " chunks，text 一致\n";
                }

                ++sum.streams;
                streamOpen = false;
                acc = SseAccumulator();
                expectedIndex = 0;
                streamChunks = 0;
                curHop.clear();
            }
        }

        if (streamOpen)
        {
            ++sum.incomplete;
            os << "  [" << curHop << "] 不完整：只有 " << streamChunks
               << " 块 chunk，没有 sse_end\n";
        }

        os << "replay: " << sum.streams << " 条流 / " << sum.chunks << " 块 chunk，"
           << "不一致 " << sum.mismatches << "，不完整 " << sum.incomplete << "\n";
        return sum;
    }

    // CLI 包装：打印结果并转成退出码。
int replay(const std::string &path)
    {
        const ReplaySummary sum = replayFile(path, std::cout);
        if (!sum.opened)
        {
            std::cerr << "mxr: 打不开 " << path << "\n";
            return 1;
        }
        if (sum.streams == 0 && sum.incomplete == 0)
        {
            std::cout << "（文件里没有 sse_chunk / sse_end 事件）\n";
        }
        return (sum.mismatches == 0 && sum.incomplete == 0) ? 0 : 1;
    }

} // namespace mxr
