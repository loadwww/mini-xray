#include "net/listener.h"

#include <stdexcept>
#include <string>

// Listener 只负责"端口"这一层的三件事：bind → listen → accept。
// 它和 Socket 是互补的两个角色：Socket 主动去连别人（客户端侧），Listener 等着别人来连（服务端侧）。

namespace mxr
{

    Listener::Listener(int port)
    {
        // 建监听套接字；失败抛异常并带上系统错误码
        handle_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (handle_ == INVALID_SOCKET)
        {
            throw std::runtime_error("socket 创建失败，错误码 " + std::to_string(WSAGetLastError()));
        }

        int optval = 1; // 1 表示开启
        if (setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR,
                       reinterpret_cast<const char *>(&optval), sizeof(optval)) == SOCKET_ERROR)
        {
            throw std::runtime_error("setsockopt 失败，错误码 " + std::to_string(WSAGetLastError()));
        }

        // 绑定本机所有网卡的指定端口
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr("0.0.0.0");
        if (bind(handle_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR)
        {
            throw std::runtime_error("connect 失败，错误码 " + std::to_string(WSAGetLastError()));
        }
        // 进入监听状态，新连接排队等待 accept
        if (listen(handle_, SOMAXCONN) < 0)
        {
            throw std::runtime_error("listen 失败，错误码 " + std::to_string(WSAGetLastError()));
        }
    }

    Listener::~Listener()
    {
        if (handle_ != INVALID_SOCKET)
        {
            closesocket(handle_);
        }
    }

    Socket Listener::accept()
    {

        SOCKET conn = ::accept(handle_, nullptr, nullptr);
        if (conn == INVALID_SOCKET)
        {
            throw std::runtime_error("accept 失败，错误码 " + std::to_string(WSAGetLastError()));
        }
        return Socket::adopt(conn);
    }

} // namespace mxr
