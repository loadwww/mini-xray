#include "store/finalize.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace mxr
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    namespace
    {

        // sessionId 只接受安全字符：防止 ".." / 绝对路径把文件写到 runs/ 之外。
        // 与 T7.1 的路径穿越防护是同一条判据，只是这里更早落地。
        bool validSessionId(const std::string &s)
        {
            if (s.empty())
                return false;
            for (char c : s)
            {
                bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
                if (!ok)
                    return false;
            }
            return true;
        }

    } // namespace

    // 先写同目录 .tmp 再 rename，保证读者只看到完整文件。
    bool atomicWrite(const fs::path &finalPath, const std::string &content)
    {
        std::error_code ec;
        fs::path tmp = finalPath;
        tmp += ".tmp";

        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        out.close();
        if (!out)
        {
            fs::remove(tmp, ec);
            return false;
        }

        if (fs::exists(finalPath, ec))
        {
            fs::remove(finalPath, ec);
            if (ec)
            {
                fs::remove(tmp, ec);
                return false;
            }
        }

        fs::rename(tmp, finalPath, ec);
        if (ec)
        {
            fs::remove(tmp, ec);
            return false;
        }
        return true;
    }

    // 从 runs 统计步数/工具/token 与降级计数，时间取事件时间。
    json buildMeta(const FinalizeInput &in)
    {
        json m;
        m["session_id"] = in.sessionId;

        long long startMs = 0, endMs = 0;
        long long stepCount = 0, toolCount = 0, totalTokens = 0;
        if (!in.runs.empty())
        {
            startMs = in.runs.front().startMs;
            endMs = in.runs.front().endMs;
            for (const auto &r : in.runs)
            {
                if (r.startMs < startMs) startMs = r.startMs;
                if (r.endMs > endMs) endMs = r.endMs;
                for (const auto &s : r.steps)
                {
                    ++stepCount;
                    toolCount += static_cast<long long>(s.tools.size());
                    totalTokens += s.usage.totalTokens;
                }
            }
        }

        m["start_ms"] = startMs;
        m["end_ms"] = endMs;
        m["run_count"] = static_cast<int>(in.runs.size());
        m["main_run_id"] = in.runs.empty() ? "" : in.runs.front().id;
        m["step_count"] = stepCount;
        m["tool_count"] = toolCount;
        m["total_tokens"] = totalTokens;
        m["parse_failed"] = in.parseFailed;
        m["dropped_events"] = in.droppedEvents;
        return m;
    }

    // 校验 sessionId、建目录、依次落盘全部文件，meta 最后写作为完成标记。
    FinalizeResult finalize(const std::string &rootDir, const FinalizeInput &in)
    {
        FinalizeResult result;
        if (!validSessionId(in.sessionId))
        {
            result.error = "invalid session id";
            return result;
        }

        std::error_code ec;
        fs::path dir = fs::path(rootDir) / in.sessionId;
        fs::create_directories(dir, ec);
        if (ec)
        {
            result.error = "create dir: " + ec.message();
            return result;
        }

        int written = 0;
        auto put = [&](const fs::path &name, const std::string &content) -> bool
        {
            if (!atomicWrite(dir / name, content))
            {
                result.error = "write failed: " + name.string();
                return false;
            }
            ++written;
            return true;
        };

        if (!put("events.jsonl", in.eventsJsonl)) return result;
        if (!put("run.json", in.runs.empty() ? json::object().dump(2)
                                             : json(in.runs[0]).dump(2))) return result;
        for (std::size_t i = 1; i < in.runs.size(); ++i)
        {
            if (!put("run-" + std::to_string(i) + ".json",
                     json(in.runs[i]).dump(2))) return result;
        }
        if (!put("graph.mmd", in.graphMmd)) return result;
        if (!put("timeline.mmd", in.timelineMmd)) return result;
        if (!put("meta.json", buildMeta(in).dump(2))) return result;

        result.ok = true;
        result.filesWritten = written;
        return result;
    }

} // namespace mxr
