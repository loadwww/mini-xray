#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace httplib { class Server; }

namespace mxr {

// ═══════════════════════════════════════════════════════════════════
//  内嵌 Web UI 服务端：路由 + 安全 + 只读 API
// ═══════════════════════════════════════════════════════════════════
//
// 【这层是干嘛的】
//
//   代理运行时把采集/还原结果实时展示在浏览器里。同一份 Run 数据，
//   M6 出了静态报告，本层出"活"的监控视图——不需要重新跑一遍代理。
//
// 【边界】
//
//   只读 runs/ 下的产物，不修改、不删除（`prune` 是 T7.2 的事）。
//   前端只调 /__xray/api/*，不假设文件路径、不读本地文件。
//
// 【为什么路径防护必须手写】
//
//   这是安全判断，不是打字：`?dir=../../etc`、URL 编码变体、符号链接、
//   绝对路径——每一类都是一种攻击面。字符串 find("..") 挡不住。
//   判据：**解析后的路径必须恰好是 root 的直接子目录**。

// 把用户给的 dir 解析成 rootDir 下的安全子目录。
// 成功：outResolved 是规范化后的绝对路径，返回 true。
// 失败：outError 写拒绝原因，返回 false。
bool resolveRunDir(const std::string &rootDir, const std::string &dir,
                   std::string &outResolved, std::string &outError);

// 读一个 run 目录的内容，组装成 API 响应 JSON。
// 半截 / 缺失文件要安全跳过（采集中的 run 每 3 秒被轮询，可能正写到一半）。
nlohmann::json buildRunApiJson(const std::string &resolvedDir);

// 把两条路由注册到 svr：GET /__xray（HTML）与 GET /__xray/api/run（JSON）。
void registerWebUi(httplib::Server &svr, const std::string &rootDir,
                   const std::string &html);

// 前端页面（AI 代写的 HTML/JS，原始字符串字面量）。
extern const char *kWebUiHtml;

} // namespace mxr
