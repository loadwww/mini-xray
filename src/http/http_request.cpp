#include "http/http_request.h"

#include "net/socket.h"

#include <cctype>
#include <stdexcept>
#include <string>

namespace mxr {

namespace {

// header 名大小写不敏感，比较前统一小写
bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// 去掉行尾的 \r（报文里每行结尾是 \r\n，按 \n 切完就会剩下这个 \r）
std::string_view chomp(std::string_view line) {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

} // namespace

const std::string* findHeader(const HttpRequest& req, std::string_view name) {
    for (const auto& kv : req.headers) {
        if (iequals(kv.first, name)) {
            return &kv.second;
        }
    }
    return nullptr;
}

bool readRequest(Socket& sock, HttpRequest& req) {
    std::string buf;              // 已经收到、还没解析的字节
    std::size_t headEnd = 0;      // 头部结束位置："\r\n\r\n" 之后第一个字节的下标

    // 循环 recv 直到收齐请求头；分隔符可能跨两次 recv，回退 3 字节再找
    char chunk[4096];
    std::size_t scanned =0;
    while(1){
        int n = sock.recvSome(chunk, sizeof(chunk));
        if(n==0)return false;
        if(n == SOCKET_ERROR){
            throw std::runtime_error("连接中断，错误码 " + std::to_string(WSAGetLastError()));
        }
        buf.append(chunk, static_cast<std::size_t>(n));
        size_t pos=buf.find("\r\n\r\n", scanned > 3 ? scanned - 3 : 0);
        if( pos==std::string::npos){
            scanned = buf.size(); 
            continue;
        }else{
            headEnd=pos+4;
            break;
        }
    }
    // 首行按空格切成方法 / 路径 / 版本
    std::string_view head(buf.data(), headEnd);
    std::size_t eol = head.find("\r\n");
    std::string_view line=head.substr(0, eol);
    std::size_t sp1 = line.find(' '),sp2 = line.find(' ', sp1 + 1);
    if(sp1==std::string::npos||sp2==std::string::npos){
        throw std::runtime_error("请求行格式错误");
    }
    line=chomp(line);
    req.method  = line.substr(0, sp1);
    req.path    = line.substr(sp1 + 1, sp2 - sp1 - 1);
    req.version = line.substr(sp2 + 1);        // 行尾的 \r 还在

    // 其余行按第一个 ':' 拆成键值，直到空行
    std::size_t p = head.find("\r\n") + 2;
    while(p<head.size()){
         std::size_t e = head.find("\r\n", p);
         if(e==std::string::npos){
            e=head.size();
         }
        line=head.substr(p,e-p);
        p=e+2;
        if(line.size()==0){
            break;
        }
        std::size_t colon = line.find(':');
        if(colon==std::string::npos){
            continue;
        }
        std::string_view key=line.substr(0,colon);
        std::string_view value=line.substr(colon+1,line.size()-colon-1);
        while (!value.empty() && value.front() == ' ') {
            value.remove_prefix(1);
        }

        req.headers.emplace(key,value);
    }

    // 按 Content-Length 补齐 body；buf 中头部之后的部分已经是 body 的开头
    std::size_t need = 0;                       // 默认没有 body
    const std::string* cl = findHeader(req, "content-length");

    if (cl != nullptr) {
        need = std::stoul(*cl);                 // 非数字时抛异常
    }

    req.body = buf.substr(headEnd);
    while(req.body.size() < need){
        int n = sock.recvSome(chunk, sizeof(chunk));
        if(n==0)return false;
        if(n == SOCKET_ERROR){
            throw std::runtime_error("连接中断，错误码 " + std::to_string(WSAGetLastError()));
        }
        req.body.append(chunk, n);
    }
    return true;
}

} // namespace mxr
