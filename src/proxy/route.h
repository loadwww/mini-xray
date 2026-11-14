#pragma once

#include <optional>
#include <string>
#include <vector>

#include "config/config.h"

// 路由层：决定"这条请求发给谁、发出去时路径长什么样"。
//
// 它在整个代理里的位置：
//   客户端请求 → [HTTP 层解析出 method/path] → 【路由】→ [转发层按 url 建连接、重发]
// 所以这个文件里不应该出现 socket、httplib、配置读取——它只做字符串判断。
// 纯函数、无状态、可反复调用：M3 之后每个进来的请求都会过一遍 resolve。
//
// match 是**路径前缀**（不是域名），例如 match="/v1" 负责
//   /v1/chat/completions、/v1/models、/v1 本身
// 但不负责 /v1beta/... —— 必须是"路径段"边界，不能是字符串前缀。

namespace mxr {

// 一条请求的路由结果。
// 注意 upstream 是**指针**而不是拷贝：配置在进程里活一辈子，拷一份纯属浪费；
// 调用方只在本次请求的处理过程中用它，不存在悬垂。
struct RouteResult {
    const Upstream* upstream = nullptr; // 命中的那条配置（读它的 url / name）
    std::string path;                   // 剥掉 match 之后的路径，必以 '/' 开头
};

// path 是否落在 prefix 这个路径段下面。
//   matchSegment("/v1", "/v1/chat/completions") → true
//   matchSegment("/v1", "/v1")                  → true
//   matchSegment("/v1", "/v1beta/x")            → false   ← 关键：'b' 不是段边界
//   matchSegment("/v1", "/v10")                 → false
//   matchSegment("/v1", "/v1x")                 → false
//   matchSegment("/v1", "/v1?x=1")              → true    ← '?' 也算边界，query 从这开始
//   matchSegment("/",   "/anything")            → true    ← 根前缀全匹配
// 判"边界"的字符只有两种：'/' 和 '?'（以及已到字符串末尾）。
bool matchSegment(const std::string& prefix, const std::string& path);

// 剥掉 prefix，返回剩下要发给上游的路径（必以 '/' 开头）。
// 只在 matchSegment 为真时调用，不必重复判断。
//   stripPrefix("/v1", "/v1/chat/completions") → "/chat/completions"
//   stripPrefix("/v1", "/v1")                  → "/"      ← 不能是空串
//   stripPrefix("/",   "/v1/chat")             → "/v1/chat"
// 剥完若不以 '/' 开头要补一个，否则拼 url 时会出现 "https://hostchat"（少斜杠）。
std::string stripPrefix(const std::string& prefix, const std::string& path);

// 按 match 长度**从长到短**找第一条命中的上游。
// 长的先试：/v1/chat 和 /v1 同时存在时，/v1/chat/completions 必须归 /v1/chat。
// 一条都没命中 → nullopt（调用方负责回 404）。注意是"没命中"，不是"出错"。
std::optional<RouteResult> resolve(const std::vector<Upstream>& ups, const std::string& path);

// 启动时校验配置，**一次**跑完就不用在每个请求里重复检查。
// 返回 nullopt 表示合法；否则返回一句给人看的错误描述（调用方直接打 fatal 退出）。
// 非法情形：match 不以 '/' 开头 / match 以 '/' 结尾（"/" 本身除外）/
//           两条 match 完全相同（等长冲突的后验版）/ url 为空。
std::optional<std::string> validateUpstreams(const std::vector<Upstream>& ups);

} // namespace mxr
