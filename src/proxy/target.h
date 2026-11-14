#pragma once

#include <optional>
#include <string>

#include "config/config.h"

// 转发目标：把"配置里的一条上游" + "剥好的路径" 变成一个真的能发出去的请求。
//
// 它在代理里的位置：
//   【路由】给出 (upstream, /chat/completions)
//        ↓
//   【buildTarget】给出 (http://127.0.0.1:9100, /v1/chat/completions)
//        ↓
//   【转发层】httplib::Client 连 origin，请求行写 path
//
// 为什么需要它：配置里的 url 是"给人看的完整地址"，而 HTTP 客户端要的是
// "连哪台机器"（scheme + host + port）和"请求行写什么路径"两件分开的事。
// 把这两件事在同一个地方拆开，转发层就不用关心配置长什么样。

namespace mxr {

struct Target {
    std::string origin; // "http://127.0.0.1:9100" —— 直接喂给 httplib::Client
    std::string path;   // "/v1/chat/completions" —— 请求行里的路径
};

// 拼一次转发目标。url 拆不出 origin（没有 scheme://host）时返回 nullopt。
//
// 输入 → 输出：
//   url="http://127.0.0.1:9100/v1", stripped="/chat/completions"
//     → { "http://127.0.0.1:9100", "/v1/chat/completions" }
//   url="https://api.openai.com",    stripped="/v1/chat/completions"
//     → { "https://api.openai.com", "/v1/chat/completions" }
//   url="http://127.0.0.1:9100/v1",  stripped="/"
//     → { "http://127.0.0.1:9100", "/v1/" }
//   url="127.0.0.1:9100"             （缺 scheme）
//     → nullopt
//
// 关键点：url 里的路径部分（示例里的 "/v1"）**不能丢**。它是被 match 剥掉的那一段，
// 上游真实路径需要它才能还原。这正是"match 和 url 必须配对"的原因：
//   match="/v1" + url=".../v1"  →  客户端 /v1/x → 上游 /v1/x（原样到达）
std::optional<Target> buildTarget(const Upstream& u, const std::string& strippedPath);

} // namespace mxr
