#pragma once

#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "common/filewriter.h"

namespace mxr {

// 按行追加 JSON 记录（JSONL）：一次 append = 一条记录一行 + '\n' + flush。
// 线程安全：append 可被多个线程同时调用，内部互斥量保证"整行一次性写入"，不会交错。
//
// 注意：这里必须 include <nlohmann/json.hpp>，因为 json 出现在公开接口上——
// 接口里露了什么类型，头文件就得 include 什么，调用方才不需要替你补头文件。
class JsonlWriter {
public:
    explicit JsonlWriter(const std::string& path);      // 打开失败 → 抛 std::runtime_error
    void append(const nlohmann::json& record);          // 写失败   → 抛 std::runtime_error

    JsonlWriter(const JsonlWriter&) = delete;           // std::mutex 本身已禁拷贝，这里写明意图
    JsonlWriter& operator=(const JsonlWriter&) = delete;

private:
    FileWriter file_;                                   // 组合：追加模式/写失败判断都复用 T1.2 的决策
    std::mutex mu_;                                     // 一把锁管一个文件（非 static，别锁到别的实例上）
};

} // namespace mxr
