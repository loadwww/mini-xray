#pragma once

#include <map>
#include <string>
#include <string_view>

namespace mxr {

class Socket;

// HttpRequest：一条已经"切好"的 HTTP 请求，纯数据。
// 它不含任何 socket / 网络知识：解析在 readRequest 里，发送由调用方决定。
// M3 会在这个结构上做转发（改 header、算 usage），所以字段先按"够用"来定。
struct HttpRequest {
    std::string method;                             // GET / POST
    std::string path;                               // /hello?x=1（暂时不拆 query，M3 需要时再说）
    std::string version;                            // HTTP/1.1
    std::map<std::string, std::string> headers;     // 键保持报文里的原样大小写
    std::string body;
};

// 从 sock 读出一个完整请求：读到 \r\n\r\n 为止的头，再按 Content-Length 补齐 body。
// 返回 false = 对端在请求完成前关闭了连接（客户端用完就走，是正常现象，不是错误）。
// 当前限制（故意的，T2.2 只做单请求单连接）：
//   - 不支持 chunked 编码的请求体（没有 Content-Length 就当没有 body）
//   - 不支持 keep-alive / pipelining：读一个请求就结束，连接由调用方关掉
bool readRequest(Socket& sock, HttpRequest& req);

// 大小写不敏感地取一个 header，取不到返回 nullptr。
// 为什么要这个函数：HTTP 的 header 名大小写不敏感，客户端可能发 Content-Length，
// 也可能发 content-length，直接 headers["Content-Length"] 会漏。
const std::string* findHeader(const HttpRequest& req, std::string_view name);

} // namespace mxr
