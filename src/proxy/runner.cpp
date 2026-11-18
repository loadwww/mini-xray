#include "proxy/runner.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "chain/link.h"
#include "chain/superstep.h"
#include "common/clock.h"
#include "config/config.h"
#include "graph/mermaid.h"
#include "proxy/server.h"
#include "proxy/spawn.h"
#include "store/finalize.h"

namespace mxr
{
    namespace fs = std::filesystem;

    namespace
    {

        // 读一个文本文件的全部内容（缺失返回空串）——收尾时 graph.mmd 等可能还没生成
        std::string readAll(const fs::path &p)
        {
            std::ifstream in(p, std::ios::binary);
            if (!in)
                return std::string();
            return std::string((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        }

        // 从 events.jsonl 还原出 Run 列表 + 出图文本 + 降级计数，组装成 FinalizeInput
        // 这一串全是纯函数模块的调用，机械劳动，已铺好。
        bool buildFinalizeInput(const fs::path &runDir, const std::string &sessionId,
                                const std::string &eventsJsonl, FinalizeInput &out)
        {
            out.sessionId = sessionId;
            out.eventsJsonl = eventsJsonl;

            // M5 还原链：读事件 → 配对 hop → 建 run → 超步归并挂工具
            auto events = readEvents((runDir / "events.jsonl").string());
            auto pair = pairHops(events);
            out.runs = buildRuns(pair.hops);
            mergeSupersteps(out.runs, pair.hops);

            // 降级计数：孤儿响应 + 坏行（坏行 readEvents 已跳过，这里用总数差估）
            out.droppedEvents = pair.orphanResponses;

            // T6.1 出图：取第一条 run（主链）生成 flowchart / gantt
            if (!out.runs.empty())
            {
                out.graphMmd = emitMermaid(out.runs[0]);
                out.timelineMmd = emitTimeline(out.runs[0]);
            }
            return true;
        }

    } // namespace

    // 起代理 + 子进程，收尾还原并落盘；子进程退出码透传。
    bool runTarget(const Config &cfg,
                   const std::vector<std::string> &command,
                   int &outChildExit,
                   std::string &outError)
    {
        /*①*/
        if (command.empty())
        {
            outError = "命令为空";
            return false;
        }
        std::string sessionId = stampNow();
        std::string runDir = cfg.root + "/runs/" + sessionId;
        std::map<std::string, std::string> env;
        std::string url = "http://127.0.0.1:" + std::to_string(cfg.port) + "/v1";
        env["OPENAI_BASE_URL"] = url;
        env["OPENAI_API_BASE"] = url;
        env["DEEPSEEK_BASE_URL"] = url;
        std::atomic<bool> stop{false};
        std::string err;

        std::thread t1(serveSession, cfg, sessionId, &stop);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::thread t2(spawnWithEnv, command, env,
               std::ref(outChildExit), std::ref(err));


        t2.join();
        stop = true;

        t1.join();
        std::string path = runDir + "/events.jsonl";
        std::string read_all = readAll(path);
        FinalizeInput out;
        if (!buildFinalizeInput(runDir, sessionId, read_all, out))
        {
            outError = "还原 events.jsonl 失败";
            return false;
        }
        FinalizeResult final = finalize(cfg.root + "/runs", out);
        if (!final.ok)
        {
            outError = "落盘失败: " + final.error;
            return false;
        }

        return final.ok;
    }

} // namespace mxr
