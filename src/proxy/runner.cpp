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

#include "cli/finish.h"
#include "common/clock.h"
#include "config/config.h"
#include "proxy/server.h"
#include "proxy/spawn.h"

namespace mxr
{
    namespace fs = std::filesystem;


    // 起代理 + 子进程，收尾还原并落盘；子进程退出码透传。
    bool runTarget(const Config &cfg,
                   const std::vector<std::string> &command,
                   int &outChildExit,
                   std::string &outError)
    {
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
        if (!finishRun(cfg.root, sessionId, outError))
            return false;

        return true;
    }

} // namespace mxr
