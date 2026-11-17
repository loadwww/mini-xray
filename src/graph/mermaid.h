#pragma once

#include <string>

#include "model/model.h"

namespace mxr {

// ═══════════════════════════════════════════════════════════════════
//  图输出层：把执行图 Run 变成人能看的文本图
// ═══════════════════════════════════════════════════════════════════
//
// 【这层是干嘛的】
//
//   M5 把流量还原成了 Run/Step/ToolExec——这是"机器关心的结论"。
//   M6 要让人在 30 秒内看懂这次任务做了什么、在哪一步出错。
//   本层把同一个 Run 变成两种文本图：flowchart（看结构）+ gantt（看时间）。
//
// 【为什么是文本图而不是 PNG】
//
//   Mermaid 源码可 diff、可 grep、可嵌 Markdown、零前端依赖；
//   PNG 要引图形库，且每次改动无法 review（二进制没有有意义的 diff）。
//
// 【边界】
//
//   只读 Run，输出 std::string；不写文件（T6.2 的 finalize 负责落盘）。
//   这样本层是纯函数，验证台不需要造临时目录。

// 转义 Mermaid 标签里的特殊字符：& " < > 换行 → HTML 实体 / <br/>。
// 标签外的节点 id、边语法不受影响；逐字符处理，不做全局 replace。
std::string escapeMermaidLabel(const std::string &s);

// 生成 flowchart TD：每个 Step 一个节点，其工具作为子节点扇出，
// Step 之间按顺序连接。失败工具按 error 分级套 classDef（红/琥珀）。
std::string emitMermaid(const Run &run);

// 生成 gantt 片段：时间轴是相对 run.startMs 的毫秒数，
// 每个 Step 一个 section，工具作为并列任务条。
std::string emitTimeline(const Run &run);

} // namespace mxr
