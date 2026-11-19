#include "store/jsonl_writer.h"

#include <mutex>
#include <stdexcept>
#include <string>

namespace mxr {

JsonlWriter::JsonlWriter(const std::string& path) : file_(path) {
    // fail fast：连文件都打不开，后面每次 append 都注定失败，不如现在就报
    if (!file_.isOpen()) {
        throw std::runtime_error("打不开 JSONL 文件: " + path);
    }
}

void JsonlWriter::append(const nlohmann::json& record) {
    // 锁外先序列化成完整一行，缩小临界区
    const std::string line = record.dump() + "\n";

    {
        std::lock_guard<std::mutex> lk(mu_);

        if (!file_.write(line)) {
            throw std::runtime_error("json write fail");
        }
        file_.flush();                          // 立即落盘：JSONL 的意义就是"写完马上能被别人读到"
    }                                           // lk 析构 → 自动解锁（提前 throw 也一样）
}

} // namespace mxr
