#pragma once

// ═══════════════════════════════════════════════════════════════════
//  run 编排层：起代理 → 起子进程 → 停代理 → 还原 → 落盘
// ═══════════════════════════════════════════════════════════════════
//
// 【这层是干嘛的】
//
//   `mxr run -- python agent.py` 一条命令背后的总指挥。它自己不实现任何
//   单一能力——起代理用 server、起子进程用 spawn、还原用 M5、出图用 T6.1、
//   落盘用 finalize。本层只负责**顺序**和**异常路径下的收尾**。
//
// 【边界】
//
//   不碰 HTTP/socket 细节、不碰 CreateProcess 细节、不碰 JSON 字段解析。
//   只做编排 + 保证"无论子进程成败，数据都落盘"。

#include <string>
#include <vector>

namespace mxr {
struct Config;

// 跑一次完整会话。
//   cfg          : 配置（port / root / upstreams）
//   command      : 子进程命令（"--" 之后的全部参数；argv[0] 是程序名）
//   outChildExit : 子进程退出码（spawn 失败时为 -1）
//   outError     : 失败原因
// 返回 true = 整条链路走完（含 finalize），false = 有环节失败（outError 说明）。
// 注意：子进程非零退出**不算** runTarget 失败——退出码透传，数据照常落盘。
bool runTarget(const Config &cfg,
               const std::vector<std::string> &command,
               int &outChildExit,
               std::string &outError);

} // namespace mxr
