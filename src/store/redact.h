#pragma once

#include <map>
#include <string>

namespace mxr {

// 脱敏（redaction）只作用于**落盘的那份副本**。
// 转发给上游的请求一个字节都不改 —— token 必须原样带上，否则业务直接 401。
// 所以这些函数只被 Recorder 的写线程调用，绝不出现在转发路径上。
//
// 为什么用正则而不是字符串查找：
//   "sk-" 开头的 key 后面跟多少位、Bearer 后面有没有引号，手写扫描要处理
//   一堆边界；正则一行就能表达"我要的形状"。这里调用频率是"每个事件一次"，
//   在写线程里跑，不在热路径，性能足够。
//
// 已知局限（先记着，M7 可以再收紧）：
//   只认 sk-xxx 和 Bearer xxx 两种最常见的形状。用户自己起名的环境变量
//   （OPENAI_API_KEY=abc123）在 header 里会被 x-api-key / authorization 规则
//   盖住，但如果它出现在 body 的某个自由文本字段里，就漏了。
//   脱敏永远做不到 100%，做到"常见路径不漏"即可。
std::string redactBody(const std::string& body);

// 这个 header 名要不要整体打码（大小写不敏感）
bool shouldMaskHeader(const std::string& name);

// 逐条复制并把敏感 header 的值换成 "***"，键统一转小写
std::map<std::string, std::string> redactHeaders(const std::map<std::string, std::string>& in);

} // namespace mxr
