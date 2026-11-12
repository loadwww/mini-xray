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
    // ① 进程级初始化：载入 ws2_32.dll、协商 2.2 版本。
    // 形状：if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { 抛 std::runtime_error }
    // 消息里带上返回值。坑：没调用它之前，任何 socket 调用都返回 WSANOTINITIALISED(10093)。
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { throw std::runtime_error("wsa初始化失败"); }
}

WsaGuard::~WsaGuard() {
    WSACleanup();
}

Socket::Socket() {
    // ② 创建 TCP 句柄：socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)，赋给 handle_。
    // 坑：SOCKET 是无符号类型，失败值是 INVALID_SOCKET（全 1），不是 -1——用 int 接、判 < 0 永远不成立。
    // 失败抛 std::runtime_error，消息里带 WSAGetLastError()。
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
    // ③ 填三个字段，每个都有坑：
    //    sin_family = AF_INET;                        ← connect 靠它判断这段内存该怎么解释，漏填报 WSAEAFNOSUPPORT
    //    sin_port   = htons(...);                     ← 主机序转网络序（x86 小端、网络大端）；port 是 int，要 static_cast<u_short>
    //    sin_addr.s_addr = inet_addr(host.c_str());   ← 点分十进制 → 网络序整数，失败返回 INADDR_NONE；不支持域名/IPv6
    addr.sin_family = AF_INET;   
    addr.sin_port   = htons(port);
    addr.sin_addr.s_addr = inet_addr(host.c_str()); 

    // ④ connect(handle_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))
    //    泛型指针强转是 C 接口没有继承关系的必然；sizeof(addr) 必须是 sockaddr_in 的大小，传小了内核按 sockaddr 读，端口错位。
    //    返回 SOCKET_ERROR 就抛 std::runtime_error，消息里带上 WSAGetLastError()
    if(connect(handle_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))==SOCKET_ERROR){
        throw std::runtime_error("connect 失败，错误码 " + std::to_string(WSAGetLastError())); 
    }
}

void Socket::sendAll(std::string_view data) {
    std::size_t sent = 0;
    // ⑤ 循环到 sent == data.size()：
    //      send(handle_, data.data() + sent, static_cast<int>(data.size() - sent), 0)
    //    返值是"这次拷进内核发送缓冲区"的字节数，可能远小于请求量，所以 sent += 返值 继续发；
    //    返回 SOCKET_ERROR 就抛异常。
    //    两个不许：不许只发一次（会截断请求）、不许每次从 data.data() 重头（重复已发字节）。
    while (sent < data.size()) {
        const int n = send(handle_, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n == SOCKET_ERROR) {
            throw std::runtime_error("send 失败，错误码 " + std::to_string(WSAGetLastError()));
        }
        sent += static_cast<std::size_t>(n);
    }
}

int Socket::recvSome(char* buf, int len) {
    // ⑥ 直接 return recv(handle_, buf, len, 0) 的结果。
    //    不要在这里"包装"成 bool 或 string —— 三种返回值（>0 / 0 / SOCKET_ERROR）语义不同，
    //    由调用方决定怎么处理，包起来就丢信息了
    return recv(handle_, buf, len, 0);
}

} // namespace mxr
