#pragma once

#include <iosfwd>
#include <string>

namespace mxr {

struct ReplaySummary {
    bool opened = false;       // 文件成功打开了吗（false = 下面几个数都没意义）
    int streams = 0;           // 见到了 sse_end 的流数
    long long chunks = 0;      // sse_chunk 总条数
    int mismatches = 0;        // 文本/usage 不一致、或 chunk_index 跳号/重复的流数
    int incomplete = 0;        // 有 chunk 但没有 sse_end 的流数（进程被杀 / 客户端断开）
};

// 逐行重放并把人类可读的结果写进 os。返回统计（不抛异常）。
ReplaySummary replayFile(const std::string &path, std::ostream &os);

// CLI 包装：打印 + 转成退出码（0 = 全部一致，1 = 打不开或有不一致/不完整）
int replay(const std::string &path);

} // namespace mxr
