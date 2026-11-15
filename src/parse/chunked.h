#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace mxr {

class ChunkedDecoder {
public:
    // 喂入一段原始字节，返回本次能解码出来的 payload（可能为空）。
    // 不足一个完整单位的字节留在内部，等下次 feed 补上。
    std::string feed(std::string_view in);

    bool done() const { return st_ == St::Done; }
    bool failed() const { return st_ == St::Failed; }

    // 内部还没消费的原始字节数（调试 / 测试用）
    std::size_t pending() const { return buf_.size(); }

private:
    // 状态机的四个活状态 + 两个终态。
    //
    //   Size     等下一条长度行（"1a3\r\n"）
    //   Data     正在搬 payload，还剩 remaining_ 字节
    //   DataCrLf 数据搬完，等结尾的 \r\n
    //   Trailer  读到长度 0 之后，跳过 trailer 直到空行
    //   Done     正常结束，后续 feed 一律返回空
    //   Failed   格式错误，后续 feed 一律返回空
    enum class St { Size, Data, DataCrLf, Trailer, Done, Failed };

    // 把一行长度文本转成数字。line **不含**结尾的 \r\n。
    //   "1a3"          → 0x1a3
    //   "1A3"          → 0x1a3     十六进制大小写都认
    //   "1a3;name=v"   → 0x1a3     分号后面是 chunk extension，丢弃
    //   ""             → nullopt   空行不合法
    //   "zz"           → nullopt   非十六进制字符
    //   长度为 0 的 "0" → 0        （不是失败！0 是结束标记）
    std::optional<std::size_t> parseSizeLine(std::string_view line) const;

    St st_ = St::Size;
    std::string buf_;             // 还没消费的原始字节
    std::size_t remaining_ = 0;   // Data 状态还剩多少字节要搬
};

} // namespace mxr
