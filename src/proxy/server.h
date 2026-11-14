#pragma once

#include "config/config.h"

// 代理服务：对外扮演服务端，对内扮演客户端。
//
// 它不 include httplib.h —— 那个头文件 31 万字符，只在 server.cpp 里 include 一次，
// 谁 include 谁就要多付几秒编译时间。对外只暴露"给一份配置，跑起来"。
//
// 阻塞直到 listen 出错或被 stop。返回进程退出码（0 正常，1 启动失败）。

namespace mxr {

int serve(const Config& cfg);

} // namespace mxr
