#pragma once

#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "common/filewriter.h"

namespace mxr {

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
