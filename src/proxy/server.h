#pragma once

#include "config/config.h"

// 代理服务：对外扮演服务端，对内扮演客户端。
//
// 它不 include httplib.h —— 那个头文件 31 万字符，只在 server.cpp 里 include 一次，
// 谁 include 谁就要多付几秒编译时间。对外只暴露"给一份配置，跑起来"。
//
// 阻塞直到 listen 出错或被 stop。返回进程退出码（0 正常，1 启动失败）。

#include <atomic>
#include <string>

namespace mxr {

// 启动代理并阻塞到停止。run 目录 = cfg.root/runs/stampNow()。
int serve(const Config& cfg);

// run 命令专用：可指定 sessionId 与外部停止信号。
//   sessionId  : 非空时 run 目录 = cfg.root/runs/<sessionId>；空则同 serve() 用时间戳。
//   externalStop: 非空指针时，代理会观察这个标志——置 true 即让 listen() 返回（正常收尾）。
// 其余行为与 serve() 完全一致（Ctrl+C 仍可用）。
int serveSession(const Config& cfg, const std::string& sessionId,
                 std::atomic<bool>* externalStop);

} // namespace mxr
