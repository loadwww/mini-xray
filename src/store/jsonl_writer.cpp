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
    // ① 锁外：把一条记录变成"完整的一行"——紧凑 JSON + 换行符，全程不碰文件
    //    提示：record.dump() 是紧凑单行；换行必须一起拼进来，不能留到锁内单独写
    const std::string line = record.dump() + "\n";

    {
        // ② 开始加锁：从这里到作用域结束，别的线程进不来
        std::lock_guard<std::mutex> lk(mu_);

        if (!file_.write(line)) {
            throw std::runtime_error("json write fail");   // ② 填错误消息（一个字符串表达式）
        }
        file_.flush();                          // 立即落盘：JSONL 的意义就是"写完马上能被别人读到"
    }                                           // lk 析构 → 自动解锁（提前 throw 也一样）
}

} // namespace mxr
