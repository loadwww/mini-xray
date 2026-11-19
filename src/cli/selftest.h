#pragma once

#include <string>
#include <vector>

namespace mxr {

struct TestOutcome {
    std::string name;
    bool passed = false;
    long long elapsedMs = 0;
};

struct SelftestSummary {
    int passed = 0;
    int failed = 0;
    std::vector<TestOutcome> outcomes;
};

// 列出所有已注册的验证台名字（一行一个，写到 stdout）
void selftestList();

// 跑测试。names 为空 = 跑全部；否则只跑指定的。
// baseDir: 验证台 exe 所在目录（一般是 mxr.exe 同目录）。
// 返回汇总（调用方据此决定退出码：failed==0 → 0，否则 1）。
SelftestSummary runSelftest(const std::string &baseDir,
                            const std::vector<std::string> &names);

} // namespace mxr
