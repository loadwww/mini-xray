#pragma once

#include <string>

namespace mxr {

// 两个钟，两种用途，别混：
//
//   steady_clock  → 测"持续了多久"。单调递增，系统改时间也不受影响。
//                   用在 server.cpp 的 latency 上。
//   system_clock  → 记"发生在几点"。能和别的机器/别的日志对齐，但会被系统时间
//                   调整影响（所以绝不能拿两个 system_clock 相减算耗时）。
//                   用在事件的 ts_ms 上。
long long nowMs();

// 拿来做目录名的时间戳，形如 "20261114-153012"。
// 为什么不用 ISO 8601（2026-11-14T15:30:12）：Windows 文件名不允许 ':'，
// 拼接出来的路径直接创建失败。日期时间文件名的通行做法就是只留数字和短横线。
std::string stampNow();

} // namespace mxr
