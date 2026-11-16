#include "chain/superstep.h"

#include <algorithm>
#include <map>

namespace mxr
{
    namespace
    {

        // 文本启发式词表：命中任何一个（不区分大小写）→ inferred。
        // 为什么是子串匹配而不是分词：工具结果的形状千变万化（可能不是自然语言），
        // 分词反而会漏掉 "myerror:xx" 这种黏在一起的写法。
        const char *kErrorWords[] = {"error", "failed", "exception", "not found"};

        std::string toLower(const std::string &s)
        {
            std::string out = s;
            for (char &c : out)
                if (c >= 'A' && c <= 'Z')
                    c = char(c - 'A' + 'a');
            return out;
        }

    } // namespace

    // 按协议证据 → 结果 error 字段 → 文本启发式的顺序给工具执行分级。
    ErrorKind classifyToolError(const std::string &resultJson, bool protocolIsError)
    {
        if (protocolIsError)
            return ErrorKind::confirmed;

        auto j = nlohmann::json::parse(resultJson, nullptr, false);
        if (!j.is_discarded() && j.is_object() && j.contains("error"))
            return ErrorKind::confirmed;

        std::string low = toLower(resultJson);
        for (const auto &word : kErrorWords)
        {
            if (low.find(word) != std::string::npos)
                return ErrorKind::inferred;
        }
        return ErrorKind::none;
    }

    // 按 tool_call_id 把上一响应的调用意图与下一请求的结果消息配对。
    std::vector<ToolExec> toolsFromDelta(const std::vector<ToolCall> &prevCalls,
                                         const std::vector<Message> &curMsgs,
                                         long long windowStartMs,
                                         long long windowMs)
    {
        std::vector<ToolExec> out;
        for (const auto &c : prevCalls)
        {
            ToolExec t;
            t.toolCallId = c.id;
            t.name = c.name;
            t.argsJson = c.argsJson;
            t.startMs = windowStartMs;
            t.durationMs = windowMs;
            t.durationInferred = true;

            for (const auto &m : curMsgs)
            {
                if (m.role == "tool" && m.toolCallId == c.id)
                {
                    t.resultJson = m.content;
                    break;
                }
            }

            bool protocolIsError = false;
            auto j = nlohmann::json::parse(t.resultJson, nullptr, false);
            if (!j.is_discarded() && j.is_object())
                protocolIsError = j.value("is_error", false);

            t.error = classifyToolError(t.resultJson, protocolIsError);
            out.push_back(t);
        }
        return out;
    }

    // 用 Step 的请求指纹反查 Hop，为每步挂上并行工具区间与执行结果。
    void mergeSupersteps(std::vector<Run> &runs, const std::vector<Hop> &hops)
    {
        std::map<std::string, int> byFp;
        for (int i = 0; i < (int)hops.size(); ++i)
        {
            auto hs = prefixHashes(hops[i].request.messages);
            if (!hs.empty())
                byFp[hs.back()] = i;
        }

        for (auto &run : runs)
        {
            std::vector<const Hop *> chain;
            for (const auto &s : run.steps)
            {
                auto it = byFp.find(s.requestFingerprint);
                chain.push_back(it != byFp.end() ? &hops[it->second] : nullptr);
            }

            for (std::size_t i = 0; i < run.steps.size(); ++i)
            {
                if (!chain[i])
                    continue;

                Step &s = run.steps[i];
                const Hop *next = (i + 1 < chain.size()) ? chain[i + 1] : nullptr;
                long long windowMs = next ? std::max(0LL, next->startMs - chain[i]->endMs) : 0;

                s.tools = toolsFromDelta(chain[i]->response.toolCalls,
                                         next ? next->request.messages : std::vector<Message>(),
                                         chain[i]->endMs, windowMs);
                if (next && !s.tools.empty())
                    s.endMs = next->startMs;
            }
        }
    }

} // namespace mxr
