#pragma once

#include <winsock2.h>

#include "net/socket.h"

namespace mxr {

// Listener：一个"等着别人来连"的监听端口。
// 职责只有三件事：占住端口（bind）、开始排队（listen）、取走一个已连上的连接（accept）。
// 它不读数据、不解析 HTTP —— 那两件事分别属于 Socket 和 http 目录。
// 生命周期 = 一个端口；进程里可以同时存在多个（M3 的 serve 只用其中一个）。
class Listener {
public:
    explicit Listener(int port);        // socket → setsockopt(SO_REUSEADDR) → bind → listen，任一步失败都抛
    ~Listener();                        // 句柄有效才 closesocket

    Listener(const Listener&) = delete;             // 一个端口只有一个主人
    Listener& operator=(const Listener&) = delete;

    Socket accept();                    // 阻塞到有连接进来；失败抛 std::runtime_error
    bool isValid() const { return handle_ != INVALID_SOCKET; }

private:
    SOCKET handle_ = INVALID_SOCKET;
};

} // namespace mxr
