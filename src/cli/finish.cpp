#include "cli/finish.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "chain/link.h"
#include "chain/superstep.h"
#include "graph/mermaid.h"
#include "store/finalize.h"

namespace mxr {
namespace fs = std::filesystem;
namespace {

std::string readAllText(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::string();
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

} // namespace

bool finishRun(const std::string &rootDir, const std::string &sessionId,
               std::string &outError)
{
    const fs::path runsRoot = fs::path(rootDir) / "runs";
    const fs::path runDir = runsRoot / sessionId;
    const std::string eventsJsonl = readAllText(runDir / "events.jsonl");
    if (eventsJsonl.empty())
    {
        outError = "events.jsonl 不存在或为空: " + runDir.string();
        return false;
    }

    FinalizeInput in;
    in.sessionId = sessionId;
    in.eventsJsonl = eventsJsonl;

    auto events = readEvents((runDir / "events.jsonl").string());
    auto pair = pairHops(events);
    in.runs = buildRuns(pair.hops);
    mergeSupersteps(in.runs, pair.hops);
    in.droppedEvents = pair.orphanResponses;

    if (!in.runs.empty())
    {
        in.graphMmd = emitMermaid(in.runs[0]);
        in.timelineMmd = emitTimeline(in.runs[0]);
    }

    FinalizeResult r = finalize(runsRoot.string(), in);
    if (!r.ok)
    {
        outError = "落盘失败: " + r.error;
        return false;
    }
    return true;
}

} // namespace mxr
