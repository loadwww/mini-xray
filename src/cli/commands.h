#pragma once

// ═══════════════════════════════════════════════════════════════════
//  CLI 子命令：show / list / diff 的实现（纯函数，直接写 stdout）
// ═══════════════════════════════════════════════════════════════════
//
// 【这层是干嘛的】
//
//   M0~M6 造出了所有零件（采集 → 还原 → 出图 → 落盘）。本层把它们
//   接到命令行上——让别人不用改代码、敲一条命令就能用这些能力。
//
// 【边界】
//
//   每个函数接收"已经解析好的参数"，负责做事 + 写 stdout/stderr，
//   返回退出码（0/1/2）。不碰 argv 解析（那是 cli.cpp 的事），
//   不抛异常（内部 catch 后返回 kExitFail）。
//
// 【为什么拆成独立函数而不是塞进 cli.cpp 的 if-else】
//
//   cli.cpp 只负责分派；每个命令的逻辑、参数、输出各在一处，
//   能独立测试（验证台直接调这些函数，不用起进程、不用抓 stdout）。

#include <string>
#include <vector>

namespace mxr {

// show <run目录>：读 run.json 打印摘要（步数/每步耗时/工具表）
// 返回 0 成功 / 1 运行时失败 / 2 用法错
int cmdShow(const std::vector<std::string> &args);

// list --root <dir> [--json]：扫 root 下每个 meta.json，按时间倒序
int cmdList(const std::vector<std::string> &args);

// diff <A> <B>：两个 run 目录按步序号对齐，输出分叉点
int cmdDiff(const std::vector<std::string> &args);

} // namespace mxr
