#pragma once

#include <string>
#include <vector>

#include "model/model.h"

namespace mxr {

// ═══════════════════════════════════════════════════════════════════
//  落盘层：把一次会话的所有产物原子地写进 runs/<sessionId>/
// ═══════════════════════════════════════════════════════════════════
//
// 【这层是干嘛的】
//
//   M4 采集事件、M5 还原执行图、M6 T6.1 生成图文本——但东西都还在内存里。
//   本层把它们一次性落盘成一份**自洽的报告包**：任何时刻打开目录，
//   要么什么都没有（还没 finalize），要么所有文件都在且互相一致。
//
// 【边界】
//
//   不生成内容（图文本由 T6.1 给、Run 由 M5 给），只负责写文件 + 摘要 + 安全。
//   不碰 HTTP、不碰 socket、不碰 CLI 参数解析。
//
// 【为什么原子写】
//
//   直接 ofstream(final) 会先截断成 0 字节再写；中途进程被杀 / 磁盘满 →
//   留下半截 JSON，下游 replay 一读就崩。写 .tmp 再 rename 保证读者看到的
//   永远是"旧文件"或"完整新文件"，没有中间态。
//
// 【为什么幂等】
//
//   Ctrl+C、重跑、CI 重复执行都可能触发多次 finalize。文件名固定 + 覆盖式
//   原子写 → 重复调用结果一致，不产生 run(1).json 这种垃圾副本。
//   时间字段取事件时间（run.startMs/endMs）而非 nowMs()，保证 meta 字节一致。

struct FinalizeInput
{
    std::string sessionId;          // 只允许 [A-Za-z0-9_-]，拒绝 .. 与绝对路径
    std::vector<Run> runs;
    std::string eventsJsonl;        // 原始采集内容（M4 的落盘结果）
    std::string graphMmd;           // T6.1 emitMermaid 的输出
    std::string timelineMmd;        // T6.1 emitTimeline 的输出
    int parseFailed = 0;            // 降级计数：解析失败的请求/响应数
    int droppedEvents = 0;          // 降级计数：孤儿响应 + 坏行数
};

struct FinalizeResult
{
    bool ok = false;
    std::string error;              // 失败原因；ok 时为空
    int filesWritten = 0;           // 成功写入的文件数
};

// 把 in 落盘到 rootDir/sessionId/，返回写入结果。
// 失败时不保证目录状态，但会尽量清掉本次产生的 .tmp 文件。
FinalizeResult finalize(const std::string &rootDir, const FinalizeInput &in);

} // namespace mxr
