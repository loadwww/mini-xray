#include "ui/webui.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

#include <httplib.h>

namespace mxr
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    // 把 dir 限定为 rootDir 的直接子目录，挡住 .. / 绝对路径 / 多级路径。
    bool resolveRunDir(const std::string &rootDir, const std::string &dir,
                       std::string &outResolved, std::string &outError)
    {
        /*①*/
        if (dir.empty())
        {
            outError = "dir为空";
            return false;
        }
        if (dir[0] == '/' || dir.find(':') != std::string::npos)
        {
            outError = "dir 是绝对路径";
            return false;
        }
        if (dir.find('/') != std::string::npos || dir.find('\\') != std::string::npos)
        {
            outError = "dir路径格式错误";
            return false;
        }
        std::error_code ec;
        fs::path rootAbs = fs::weakly_canonical(fs::absolute(rootDir, ec), ec);
        if (ec)
        {
            outError = "resolve root: " + ec.message();
            return false;
        }
        fs::path candidate = fs::weakly_canonical(rootAbs / dir, ec);
        if (ec)
        {
            outError = "resolve candidate: " + ec.message();
            return false;
        }
        if (candidate.parent_path() == rootAbs)
        {
            outResolved = candidate.string();
            return true;
        }
        outError = "目录错误";
        return false;
    }

    // 读 run 目录产物并组装前端 JSON，坏文件安全回落。
    json buildRunApiJson(const std::string &resolvedDir)
    {
        /*②*/
        json out;

        out["ok"] = true;
        out["dir"] = fs::path(resolvedDir).filename().string();

        {
            std::ifstream in(resolvedDir + "/meta.json", std::ios::binary);

            if (!in)
            {
                out["meta"] = json::object();
            }
            else
            {
                std::string text((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
                auto j = json::parse(text, nullptr, false);
                if (!j.is_discarded() && j.is_object())
                    out["meta"] = j;
                else
                    out["meta"] = json::object();
                in.close();
            }
        }
        {
            std::ifstream in(resolvedDir + "/run.json", std::ios::binary);

            if (!in)
            {
                out["steps"] = json::array();
            }
            else
            {
                std::string text((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
                auto j = json::parse(text, nullptr, false);
                if (!j.is_discarded() && j.is_object())
                {
                    json steps;
                    for (const auto &step : j["steps"])
                    {
                        json s;
                        s["index"] = step.value("index", 0);
                        s["model"] = step.value("model", std::string());
                        long long startMs = step.value("start_ms", 0LL);
                        long long endMs = step.value("end_ms", 0LL);
                        s["duration_ms"] = endMs - startMs;
                        long long tokens = 0;
                        if (step.contains("usage") && step["usage"].is_object())
                            tokens = step["usage"].value("total_tokens", 0LL);
                        s["tokens"] = tokens;
                        s["output"] = step.value("output_text", std::string());

                        steps.push_back(s);
                    }
                    out["steps"] = steps;
                }

                else
                    out["steps"] = json::array();
                in.close();
            }
        }
        {
            std::ifstream in(resolvedDir + "/graph.mmd", std::ios::binary);

            if (!in)
            {
                out["mermaid"] = "";
            }
            else
            {
                std::string text((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
                out["mermaid"] = text;
                in.close();
            }
        }
        return out;
    }

    // 注册 HTML 页、会话列表与只读 run API 三条路由。
    void registerWebUi(httplib::Server &svr, const std::string &rootDir,
                       const std::string &html)
    {
        /*③*/
        svr.Get("/__xray", [html](const httplib::Request &req, httplib::Response &res)
                {
                    res.status = 200;
                    res.set_content(html, "text/html; charset=utf-8"); });

        svr.Get("/__xray/api/runs", [rootDir](const httplib::Request &req, httplib::Response &res)
                {
                    std::vector<std::string> names;
                    std::error_code ec;
                    for (const auto &entry : fs::directory_iterator(rootDir, ec))
                    {
                        if (entry.is_directory(ec) && !ec)
                            names.push_back(entry.path().filename().string());
                    }
                    std::sort(names.begin(), names.end(),
                              [](const std::string &a, const std::string &b) { return a > b; });
                    json j;
                    j["runs"] = names;
                    res.status = 200;
                    res.set_content(j.dump(), "application/json; charset=utf-8");
                });

        svr.Get("/__xray/api/run", [rootDir](const httplib::Request &req, httplib::Response &res)
                {
                    if(! req.has_param("dir")){
                        res.status=400;
                        return ;
                    }
                    std::string dir= req.get_param_value("dir"); 
                    std::string outResolved,outError;
    
                    if(!resolveRunDir(rootDir, dir, outResolved, outError)){
                        res.status=400;
                        return ;
                    }
                    json j= buildRunApiJson(outResolved);
                    res.set_content(j.dump(), "application/json; charset=utf-8");
                    });
    }

} // namespace mxr
