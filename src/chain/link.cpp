#include "chain/link.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>

namespace mxr
{
    namespace
    {

        constexpr const char *kHashSeed = "mxr-seed";

        // FNV-1a 64 位：不依赖 std::hash（它的结果允许随实现/进程变化，做不了稳定 id）
        std::string fnv1a(const std::string &s)
        {
            std::uint64_t h = 14695981039346656037ull;
            for (unsigned char c : s)
            {
                h ^= c;
                h *= 1099511628211ull;
            }
            char buf[17];
            std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
            return std::string(buf);
        }

    } // namespace

    // 读 events.jsonl：坏行跳过，文件打不开就返回空（不抛）。
    std::vector<nlohmann::json> readEvents(const std::string &path)
    {
        std::ifstream in(path);
        std::vector<nlohmann::json> out;
        std::string line;
        while (std::getline(in, line))
        {
            auto j = nlohmann::json::parse(line, nullptr, false);
            if (!j.is_discarded() && j.is_object())
                out.push_back(std::move(j));
        }
        return out;
    }

    // 一条消息的稳态哈希。用 \x1f 分隔字段，避免 "ab"+"c" 和 "a"+"bc" 撞在一起。
    std::string messageHash(const Message &m)
    {
        return fnv1a(m.role + "\x1f" + m.content + "\x1f" + m.toolCallId + "\x1f" + m.name);
    }

    // 滚动前缀哈希的单步：把当前消息链式接到前一个哈希上。
    std::string chainHash(const std::string &prev, const Message &m)
    {
        return fnv1a(prev + ":" + messageHash(m));
    }

    // 朴素全量版：逐字段比较，用于和哈希版对账。
    bool isStrictPrefix(const std::vector<Message> &a, const std::vector<Message> &b)
    {
        if (a.empty() || a.size() >= b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].role != b[i].role || a[i].content != b[i].content || a[i].toolCallId != b[i].toolCallId || a[i].name != b[i].name)
            {
                return false;
            }
        }
        return true;
    }

    // 稳定 id 的三要素：根事件时间、根指纹、该 run 在文件里的序号。
    std::string makeRunId(long long rootTsMs, const std::string &rootHash, int index)
    {
        return "run-" + std::to_string(rootTsMs) + "-" + rootHash.substr(0, 8) + "-" + std::to_string(index);
    }

    // 按 hop 把 req / resp 配成对；流式 hop 用 sse_end 补结论。
    PairResult pairHops(const std::vector<nlohmann::json> &events)
    {
        PairResult out;
        std::map<std::string, Hop> byHop;
        for (const auto &ev : events)
        {
            std::string hop = ev.value("hop", "");
            std::string type = ev.value("type", "");
            if (type == "req")
            {
                byHop[hop].hop = hop;
                byHop[hop].startMs = ev.value("ts_ms", 0LL);
                byHop[hop].seq = ev.value("seq", 0LL);
                byHop[hop].method = ev.value("method","");
                byHop[hop].path = ev.value("path", "");
                byHop[hop].request = mxr::parseRequest(ev.value("body", ""));
            }
            else if (type == "resp")
            {
                byHop[hop].endMs = ev.value("ts_ms", 0LL);
                byHop[hop].status = ev.value("status",0);
                byHop[hop].response = mxr::parseResponse(ev.value("body", ""));
            }
            else if (type == "sse_end")
            {
                byHop[hop].endMs = ev.value("ts_ms", 0LL);
                byHop[hop].response = mxr::parseSseEnd(ev);
            }
        }
        for (const auto &[k, v] : byHop)
        {
            if (v.hop.empty())
            {
                out.orphanResponses++;
                continue;
            }
            else
            {
                out.hops.push_back(v);
            }
        }
        sort(out.hops.begin(), out.hops.end(), [](const auto &a, const auto &b)
             { return a.seq < b.seq; });
        return out;
    }

    // 计算 messages 每个前缀的链哈希：hashes[i] 是前 i+1 条消息的指纹。
    std::vector<std::string> prefixHashes(const std::vector<Message> &messages)
    {
        std::vector<std::string> out;
        std::string prev = kHashSeed;
        for (const auto &m : messages)
        {
            prev = chainHash(prev, m);
            out.push_back(prev);
        }
        return out;
    }

    // 按序处理每个 hop：从最近的已处理 hop 往前找严格前缀作父步，
    // 有父就往父所属的 run 追加 Step，没有就开新 run（id 用稳定要素生成）。
    std::vector<Run> buildRuns(const std::vector<Hop> &hops)
    {
        std::vector<Run> runs;
        std::vector<int> hopRun(hops.size(),-1);
        std::vector<std::vector<std::string>> hashs;
        for (size_t i = 0; i < hops.size(); i++)
        {
            Hop B = hops[i];
            std::vector<std::string> hashB = prefixHashes(B.request.messages);
            hashs.push_back(hashB);
            int parent = -1;
            for (int j = (int)i - 1; j >= 0; j--)
            {
                Hop A = hops[j];
                std::vector<std::string> hashA = hashs[j];
                if (hashA.empty() || hashA.size() >= hashB.size())
                    continue;
                if (hashB[hashA.size() - 1] != hashA.back())
                {
                    continue;
                }
                int sysA = 0;
                for (const auto &m : A.request.messages)
                {
                    if (m.role != "system")
                        break;
                    sysA++;
                }
                if (A.request.messages.size() <= sysA)
                    continue;
                parent = j;
                break;
            }
            Step s;
            s.index = parent >= 0 ? runs[hopRun[parent]].steps.size() : 0;
            s.startMs = B.startMs;
            s.endMs = B.endMs;
            s.model = B.request.model;
            s.requestFingerprint = hashB.empty() ? "" : hashB.back();
            s.outputText = B.response.outputText;
            s.usage = B.response.usage;
            s.complete = true;
            if (parent >= 0)
            {
                auto &R = runs[hopRun[parent]];
                R.steps.push_back(s);
                R.endMs = B.endMs;
                hopRun[i] = hopRun[parent];
            }
            else
            {
                Run R;
                R.id = makeRunId(B.startMs, s.requestFingerprint, runs.size());
                R.startMs = B.startMs;
                R.endMs = B.endMs;
                R.steps.push_back(s);
                hopRun[i] = runs.size();
                runs.push_back(R);
            }
        }
        return runs;
    }

} // namespace mxr
