// T2.1 验证台（AI 写的，你只需要让 Socket 通过它）
// 用法：socket_check [host] [port] [path]
//   默认 127.0.0.1 8000 /
// 做的事：连上 → 发一个最小 HTTP/1.1 请求 → 循环 recv 到对端关闭 → 打印收到的字节数和前 500 字节

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "net/socket.h"

int main(int argc, char* argv[]) {
    const std::string host = (argc > 1) ? argv[1] : "127.0.0.1";
    const int         port = (argc > 2) ? std::atoi(argv[2]) : 8000;
    const std::string path = (argc > 3) ? argv[3] : "/";

    try {
        mxr::WsaGuard wsa;              // 进程级初始化，作用域 = 整个 main
        mxr::Socket   sock;
        sock.connectTo(host, port);

        const std::string request = "GET " + path + " HTTP/1.1\r\n" +
                                    "Host: " + host + "\r\n" +
                                    "Connection: close\r\n" +   // 让服务端响应完主动关，recv 才会返回 0
                                    "\r\n";
        sock.sendAll(request);

        std::string received;
        char buf[4096];
        for (;;) {
            const int n = sock.recvSome(buf, sizeof(buf));
            if (n == 0) {
                break;                  // 对端关闭（FIN），读完了
            }
            if (n < 0) {
                std::cerr << "recv 失败，错误码 " << WSAGetLastError() << "\n";
                return 1;
            }
            received.append(buf, static_cast<std::size_t>(n));
        }

        std::cout << "收到 " << received.size() << " 字节：\n";
        std::cout.write(received.data(), static_cast<std::streamsize>(std::min<std::size_t>(500, received.size())));
        std::cout << "\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << "\n";
        return 1;
    }
}
