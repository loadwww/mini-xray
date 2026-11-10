#pragma once
#include <fstream>
#include <string_view>
#include <string>

namespace mxr {

class FileWriter {
public:
    explicit FileWriter(const std::string& path);
    ~FileWriter();

    FileWriter(const FileWriter&) = delete;              // 禁拷贝
    FileWriter& operator=(const FileWriter&) = delete;

    FileWriter(FileWriter&& other) noexcept;             // 移动
    FileWriter& operator=(FileWriter&& other) noexcept;

    bool write(std::string_view text);
    void flush();
    bool isOpen();

private:
    std::ofstream file_;
};

} // namespace mxr
