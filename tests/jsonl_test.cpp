// T1.4 验证台（AI 写的，你只需要让你的 JsonlWriter 通过它）
//   场景 1：单线程写 3 条 → 文件里必须正好 3 行，每行能单独 parse
//   场景 2：4 线程 × 100 条 → 必须正好 400 行，每行能单独 parse，且 json 不残缺
// 用法：build/jsonl_test      （退出码 0 = 全过，1 = 有失败项）

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "store/jsonl_writer.h"

namespace {

std::vector<std::string> readLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

// 逐行 parse，返回解析失败的行号列表（从 1 开始）
std::vector<int> badLines(const std::vector<std::string>& lines) {
    std::vector<int> bad;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!nlohmann::json::accept(lines[i])) {
            bad.push_back(static_cast<int>(i) + 1);
        }
    }
    return bad;
}

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << what << "\n";
    if (!ok) ++g_failures;
}

} // namespace

int main() {
    const std::string path = "jsonl_test.jsonl";
    std::remove(path.c_str());

    std::cout << "场景 1：单线程写 3 条\n";
    {
        mxr::JsonlWriter w(path);
        w.append({{"seq", 1}, {"tool", "read_file"}});
        w.append({{"seq", 2}, {"tool", "grep"}});
        w.append({{"seq", 3}, {"tool", "write_file"}});
    }
    {
        auto lines = readLines(path);
        check(lines.size() == 3, "行数 == 3（实际 " + std::to_string(lines.size()) + "）");
        auto bad = badLines(lines);
        check(bad.empty(), "每行都能单独 parse" + (bad.empty() ? "" : "（坏行 " + std::to_string(bad.size()) + " 条）"));
    }

    std::cout << "场景 2：4 线程 × 100 条\n";
    std::remove(path.c_str());
    {
        mxr::JsonlWriter w(path);
        std::vector<std::thread> workers;
        for (int t = 0; t < 4; ++t) {
            workers.emplace_back([&w, t] {
                for (int i = 0; i < 100; ++i) {
                    w.append({{"thread", t}, {"i", i}});
                }
            });
        }
        for (auto& th : workers) {
            th.join();
        }
    }
    {
        auto lines = readLines(path);
        check(lines.size() == 400, "行数 == 400（实际 " + std::to_string(lines.size()) + "）");
        auto bad = badLines(lines);
        check(bad.empty(), "400 行全部是完整 JSON（坏行 " + std::to_string(bad.size()) + " 条）");
    }

    std::cout << (g_failures == 0 ? "全部通过\n" : "有失败项\n");
    return g_failures == 0 ? 0 : 1;
}
