#include "cli/commands.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace mxr
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    namespace
    {

        constexpr int kOk = 0;
        constexpr int kFail = 1;
        constexpr int kUsage = 2;

        // 从 argv 风格的参数里取 --flag 的值：找到 flag 就返回下一个参数，否则空
        std::string flagValue(const std::vector<std::string> &args, const std::string &flag)
        {
            for (std::size_t i = 0; i < args.size(); ++i)
                if (args[i] == flag && i + 1 < args.size())
                    return args[i + 1];
            return std::string();
        }

        bool hasFlag(const std::vector<std::string> &args, const std::string &flag)
        {
            return std::find(args.begin(), args.end(), flag) != args.end();
        }

        // 位置参数（不以 '-' 开头、且不是某个 flag 的值）
        std::vector<std::string> positionals(const std::vector<std::string> &args)
        {
            std::vector<std::string> out;
            for (std::size_t i = 0; i < args.size(); ++i)
            {
                const std::string &a = args[i];
                if (!a.empty() && a[0] == '-')
                {
                    // --root / --run 这类带值的 flag，跳过它的值
                    if ((a == "--root" || a == "--run") && i + 1 < args.size())
                        ++i;
                    continue;
                }
                out.push_back(a);
            }
            return out;
        }

        json readJsonFile(const fs::path &path, bool &ok)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                ok = false;
                return json::object();
            }
            std::string text((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
            auto j = json::parse(text, nullptr, false);
            if (j.is_discarded() || !j.is_object())
            {
                ok = false;
                return json::object();
            }
            ok = true;
            return j;
        }

    } // namespace

    // 读 run.json 打印摘要、每步耗时与工具表。
    int cmdShow(const std::vector<std::string> &args)
    {
        /*①*/
        auto pos = positionals(args);
        if (pos.empty()) { std::cerr << "mxr show: 缺少 run 目录\n"; return kUsage; }
        bool ok;
        json run = readJsonFile(fs::path(pos[0]) / "run.json", ok);
        if (!ok) { std::cerr << "mxr show: 读不到 run.json: " << pos[0] << "\n"; return kFail; }
        int tools = 0, tokens = 0;
        json steps = run.contains("steps") && run["steps"].is_array() ? run["steps"] : json::array();
        for (const auto &s : steps) {
            tools += (s.contains("tools") && s["tools"].is_array()) ? (int)s["tools"].size() : 0;
            if (s.contains("usage") && s["usage"].is_object())
                tokens += s["usage"].value("total_tokens", 0);
        }
        std::cout << "Run: " << run.value("id", std::string()) << " · " << steps.size()
                  << " Step · " << tools << " 工具 · " << tokens << " token\n";
        for (std::size_t i = 0; i < steps.size(); ++i) {
            const auto &s = steps[i];
            long long st = s.value("start_ms", 0LL), en = s.value("end_ms", 0LL);
            std::cout << "  S" << i << "  " << s.value("model", std::string())
                      << "  " << (en - st) << "ms  " << tokens << "tok  complete\n";
            if (s.contains("tools") && s["tools"].is_array())
                for (std::size_t j = 0; j < s["tools"].size(); ++j) {
                    const auto &t = s["tools"][j];
                    std::cout << "    T" << j << " " << t.value("name", std::string())
                              << "  " << t.value("duration_ms", 0LL) << "ms  "
                              << t.value("error", std::string("none")) << "\n";
                }
        }
        return kOk;
    }

    // 扫描会话目录的 meta.json，按开始时间倒序列出。
    int cmdList(const std::vector<std::string> &args)
    {
        /*②*/
        std::string root = flagValue(args, "--root");
        if (root.empty()) root = "runs";
        json arr = json::array();
        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(root, ec)) {
            if (!entry.is_directory(ec) || ec) continue;
            bool ok;
            json meta = readJsonFile(entry.path() / "meta.json", ok);
            if (!ok) continue;
            json row;
            row["dir"] = entry.path().filename().string();
            row["id"] = meta.value("id", std::string());
            row["start_ms"] = meta.value("start_ms", 0LL);
            row["step_count"] = meta.value("step_count", 0);
            row["total_tokens"] = meta.value("total_tokens", 0LL);
            arr.push_back(row);
        }
        std::sort(arr.begin(), arr.end(), [](const json &a, const json &b) {
            return a.value("start_ms", 0LL) > b.value("start_ms", 0LL);
        });
        if (hasFlag(args, "--json")) {
            std::cout << arr.dump(2) << "\n";
        } else {
            for (const auto &r : arr)
                std::cout << r.value("dir", std::string()) << "  "
                          << r.value("step_count", 0) << " steps  "
                          << r.value("total_tokens", 0LL) << " tok\n";
        }
        return kOk;
    }

    // 按步序号对齐两个 run，报告首个分叉点与字段变化。
    int cmdDiff(const std::vector<std::string> &args)
    {
        /*③*/
        auto pos = positionals(args);
        if (pos.size() != 2) { std::cerr << "mxr diff: 需要两个 run 目录\n"; return kUsage; }
        bool okA, okB;
        json a = readJsonFile(fs::path(pos[0]) / "run.json", okA);
        json b = readJsonFile(fs::path(pos[1]) / "run.json", okB);
        if (!okA || !okB) { std::cerr << "mxr diff: 读不到 run.json\n"; return kFail; }
        json sa = a.contains("steps") && a["steps"].is_array() ? a["steps"] : json::array();
        json sb = b.contains("steps") && b["steps"].is_array() ? b["steps"] : json::array();
        bool found = false;
        std::size_t n = std::max(sa.size(), sb.size());
        for (std::size_t i = 0; i < n; ++i) {
            if (i >= sa.size()) {
                if (!found) { std::cout << "分叉点：Step " << i << "\n"; found = true; }
                std::cout << "  + 新增 S" << i << "\n";
                continue;
            }
            if (i >= sb.size()) {
                if (!found) { std::cout << "分叉点：Step " << i << "\n"; found = true; }
                std::cout << "  - 删除 S" << i << "\n";
                continue;
            }
            std::string ma = sa[i].value("model", std::string()), mb = sb[i].value("model", std::string());
            long long ta = 0, tb = 0;
            if (sa[i].contains("usage") && sa[i]["usage"].is_object()) ta = sa[i]["usage"].value("total_tokens", 0LL);
            if (sb[i].contains("usage") && sb[i]["usage"].is_object()) tb = sb[i]["usage"].value("total_tokens", 0LL);
            if (ma != mb || ta != tb) {
                if (!found) { std::cout << "分叉点：Step " << i << "\n"; found = true; }
                if (ma != mb) std::cout << "  S" << i << " model: " << ma << " -> " << mb << "\n";
                if (ta != tb) std::cout << "  S" << i << " tokens: " << ta << " -> " << tb << "\n";
            }
        }
        if (!found) std::cout << "两个 run 一致\n";
        return kOk;
    }

} // namespace mxr
