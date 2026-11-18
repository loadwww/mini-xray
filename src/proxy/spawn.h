#pragma once

// ═══════════════════════════════════════════════════════════════════
//  子进程模块：起一个带环境变量注入的子进程，等它结束，返回退出码
// ═══════════════════════════════════════════════════════════════════
//
// 【这层是干嘛的】
//
//   run 命令要执行 `mxr run -- python agent.py`：代理先跑起来，
//   然后用一个注入了 OPENAI_BASE_URL 的环境起 python。本模块只管"起进程 +
//   注入环境 + 等退出"这一段，不碰代理、不碰采集、不碰落盘。
//
// 【边界】
//
//   平台相关代码集中在 .cpp 的 #ifdef _WIN32 里；对外只有这一个函数。
//   不抛异常：失败返回 false + 写 errorMessage（因为调用方 runTarget 要
//   在失败时仍走 finalize 收尾，异常会跳过那一步）。

#include <map>
#include <string>
#include <vector>

namespace mxr {

// 起子进程并等待结束。
//   argv           : 要执行的命令及参数（argv[0] 是程序名）
//   extraEnv       : 要注入/覆盖的环境变量（key 大小写不敏感，Windows 语义）
//   outExitCode    : 成功时写入子进程的退出码
//   outError       : 失败时写入原因
// 返回 true = 子进程正常启动并跑完（不论退出码是多少）；false = 启动失败。
bool spawnWithEnv(const std::vector<std::string> &argv,
                  const std::map<std::string, std::string> &extraEnv,
                  int &outExitCode,
                  std::string &outError);

} // namespace mxr
