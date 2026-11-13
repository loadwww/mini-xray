#include "net/listener.h"

#include <stdexcept>
#include <string>

// Listener 只负责"端口"这一层的三件事：bind → listen → accept。
// 它和 Socket 是互补的两个角色：Socket 主动去连别人（客户端侧），Listener 等着别人来连（服务端侧）。

namespace mxr {

Listener::Listener(int port) {
    // ① 建监听用的套接字（和客户端一样：AF_INET / SOCK_STREAM / IPPROTO_TCP），赋给 handle_。
    //    失败（== INVALID_SOCKET）抛 std::runtime_error，带 WSAGetLastError()。
    handle_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle_ == INVALID_SOCKET) {
        throw std::runtime_error("socket 创建失败，错误码 " + std::to_string(WSAGetLastError()));
    }

    // ② 允许端口复用：setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR, ...)。
    //    参数是个 const char*，指向一个 int/int 布尔值，长度 sizeof(int)。
    //    坑：不设它，程序刚退出就立刻重启会 bind 失败 10048（WSAEADDRINUSE）——
    //    上一个进程的端口还在 TIME_WAIT 里没放干净。失败也抛异常。
    int optval = 1;                     // 1 表示开启
    if (setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR) {
        throw std::runtime_error("setsockopt 失败，错误码 " + std::to_string(WSAGetLastError()));
    }

    // ③ 占住端口：先把 sockaddr_in 填好（AF_INET / htons(port) / INADDR_ANY），再 bind。
    //    INADDR_ANY = 0.0.0.0，表示"本机所有网卡都收"，这和服务端的语义一致（客户端才需要指定具体地址）。
    //    bind 返回 SOCKET_ERROR 就抛，带 WSAGetLastError()（端口被占是 10048）。
    sockaddr_in addr{};
    addr.sin_family = AF_INET;   
    addr.sin_port   = htons(port);
    addr.sin_addr.s_addr = inet_addr("0.0.0.0"); 
    if(bind(handle_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))==SOCKET_ERROR){
        throw std::runtime_error("connect 失败，错误码 " + std::to_string(WSAGetLastError())); 
    }
    // ④ 开始排队：listen(handle_, backlog)，backlog 是"还没被 accept 的连接最多排多少个"。
    //    它不建立连接，只是告诉内核"这个句柄从今往后是监听用的，请把新连接放进队列"。
    //    传 SOMAXCONN 让系统用默认上限。失败抛异常。
    if (listen(handle_, SOMAXCONN) < 0) {
        throw std::runtime_error("listen 失败，错误码 " + std::to_string(WSAGetLastError())); 
    }
}

Listener::~Listener() {
    if (handle_ != INVALID_SOCKET) {
        closesocket(handle_);
    }
}

Socket Listener::accept() {
    // ⑤ 先调 Winsock 的 accept 拿到"已经连上"的新句柄，失败返回 INVALID_SOCKET。
    //    坑：必须写 ::accept —— 成员函数里不带限定的 accept 会先找到 Listener::accept（本函数自己），
    //    类作用域的名字会"挡住"全局名字（名字隐藏），于是报"函数不接受 3 个参数"。
    //    :: 前缀 = 明确要全局命名空间里的那个。新句柄和 handle_ 是两个不同的套接字：
    //    handle_ 继续待命，新句柄才是这条连接的通道。
    //    用 Socket::adopt(...) 把新句柄包成 Socket —— 不能走 Socket() 构造，那个只建空句柄、不会连。
    //    失败抛 std::runtime_error，带 WSAGetLastError()。
    SOCKET conn = ::accept(handle_, nullptr, nullptr);
    if (conn == INVALID_SOCKET) { 
        throw std::runtime_error("accept 失败，错误码 " + std::to_string(WSAGetLastError())); 
    }
    return Socket::adopt(conn);

}

} // namespace mxr
