#include "net/socket.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

// 本文件只做两件事：把 Winsock 的进程级初始化包成 RAII，把一条 TCP 连接包成对象。
// 它负责"搬运字节"，不负责判断报文边界（找 \r\n\r\n、算 Content-Length 是 M3 解析层的事）。

namespace mxr {

WsaGuard::WsaGuard() {
    WSADATA wsa{};

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { throw std::runtime_error("wsa初始化失败"); }
}

WsaGuard::~WsaGuard() {
    WSACleanup();
}

Socket::Socket() {

    handle_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle_ == INVALID_SOCKET) {
        throw std::runtime_error("socket 创建失败，错误码 " + std::to_string(WSAGetLastError()));
    }
}

Socket::~Socket() {
    if (handle_ != INVALID_SOCKET) {
        closesocket(handle_);
    }
}

// 接管 accept() 的成果。这两行是纯搬管道，不涉及任何 socket 调用，所以直接给出。
// 关键点：这里不调用 connect，句柄已经是"连着的"，再 connect 一次必失败（WSAEISCONN）。
Socket::Socket(SOCKET handle, AdoptTag) : handle_(handle) {}

Socket Socket::adopt(SOCKET handle) {
    if (handle == INVALID_SOCKET) {
        throw std::runtime_error("adopt 收到无效句柄");
    }
    return Socket(handle, AdoptTag{});      // C++17 保证这里不产生临时对象（拷贝省略）
}

Socket::Socket(Socket&& other) noexcept : handle_(other.handle_) {
    other.handle_ = INVALID_SOCKET;      // 移交后源变空句柄，它的析构不会重复关
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        if (handle_ != INVALID_SOCKET) {
            closesocket(handle_);        // 先还掉自己手里的，再接别人的
        }
        handle_ = other.handle_;
        other.handle_ = INVALID_SOCKET;
    }
    return *this;
}

void Socket::connectTo(const std::string& host, int port) {
    sockaddr_in addr{};

    addr.sin_family = AF_INET;   
    addr.sin_port   = htons(port);
    addr.sin_addr.s_addr = inet_addr(host.c_str()); 

    // 发起连接；失败抛异常并带上系统错误码
    if(connect(handle_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))==SOCKET_ERROR){
        throw std::runtime_error("connect 失败，错误码 " + std::to_string(WSAGetLastError())); 
    }
}

void Socket::sendAll(std::string_view data) {
    std::size_t sent = 0;
    // send 可能只发出一部分，按已发字节数推进直到发完
    while (sent < data.size()) {
        const int n = send(handle_, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n == SOCKET_ERROR) {
            throw std::runtime_error("send 失败，错误码 " + std::to_string(WSAGetLastError()));
        }
        sent += static_cast<std::size_t>(n);
    }
}

int Socket::recvSome(char* buf, int len) {
    // 保留 recv 的三种返回值（>0 / 0 / SOCKET_ERROR），由调用方决定怎么处理
    return recv(handle_, buf, len, 0);
}

} // namespace mxr
