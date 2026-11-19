#pragma once

// 跨平台收尾：从已落盘的 events.jsonl 还原 Run、出图、finalize 成完整报告包。
// 与 spawn 无关——Windows 的 run 命令和 Linux 的 serve + finish 流程共用这一份。

#include <string>

namespace mxr {

// rootDir 是 runs/ 的父目录（通常是 config.root）；sessionId 是 runs/ 下的子目录名。
// 成功返回 true；失败写 outError。
bool finishRun(const std::string &rootDir, const std::string &sessionId,
               std::string &outError);

} // namespace mxr
