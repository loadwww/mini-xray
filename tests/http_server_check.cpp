// T2.2 验证台（AI 写的，你只需要让 Listener + readRequest 通过它）
// 用法：http_server_check [port]        默认 9000
// 做的事：监听 → 连续处理 3 个连接 → 每个连接读一个请求、打印请求行和 header、回一个 JSON → 退出
// 为什么只处理 3 个：跑一次就知道结果，不用手动 Ctrl+C 掐进程
// 验证办法：
//   ./build/http_server_check.exe 9000
//   curl -v http://127.0.0.1:9000/hello
//   curl -v -X POST -d "abc" http://127.0.0.1:9000/echo

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "http/http_request.h"
#include "net/listener.h"
#include "net/socket.h"

int main(int argc, char* argv[]) {
    const int port = (argc > 1) ? std::atoi(argv[1]) : 9000;

    try {
        mxr::WsaGuard wsa;                       // 进程级初始化，必须比所有 socket 先构造、后析构
        mxr::Listener listener(port);
        std::cout << "listening on 127.0.0.1:" << port << "\n";

        for (int round = 1; round <= 3; ++round) {
            mxr::Socket conn = listener.accept();       // 阻塞等下一个人来连
            std::cout << "--- 连接 " << round << " ---\n";

            mxr::HttpRequest req;
            if (!mxr::readRequest(conn, req)) {
                std::cout << "[对端在请求完成前关闭]\n";
                continue;
            }

            std::cout << "请求行: " << req.method << " " << req.path << " " << req.version << "\n";
            for (const auto& kv : req.headers) {
                std::cout << "  " << kv.first << ": " << kv.second << "\n";
            }
            std::cout << "body(" << req.body.size() << " 字节): " << req.body << "\n";

            // 固定格式的响应：必须有 Content-Length，否则 curl 会一直等到连接关闭
            const std::string payload = "{\"echo\":\"" + req.path + "\"}";
            const std::string resp = "HTTP/1.1 200 OK\r\n"
                                     "Content-Type: application/json\r\n"
                                     "Content-Length: " + std::to_string(payload.size()) + "\r\n"
                                     "Connection: close\r\n"
                                     "\r\n" + payload;
            conn.sendAll(resp);
        }                                        // conn 在循环末尾析构 → 关闭这条连接

        std::cout << "处理完 3 个连接，正常退出\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << "\n";
        return 1;
    }
}
