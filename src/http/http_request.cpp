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
    std::size_t headEnd = 0;      // 头部结束位置："\r\n\r\n" 后面那个字节的下标，① 负责填它

    // ① 攒头：循环 recv，直到 buf 里出现 "\r\n\r\n"。
    //    要点：
    //      - 每次 recv 进一个局部 chunk（4KB），append 到 buf
    //      - 用 buf.find("\r\n\r\n", scanned) 找分隔符；scanned 表示"已经确认没有分隔符的长度"，
    //        每次从 max(scanned-3, 0) 接着找 —— 退 3 个字节是因为分隔符可能正好被切成两次 recv
    //        （"…\r\n" + "\r\n…"），退少了会漏，退多了只是多扫几个字节
    //      - 返回 npos 表示还没收全，继续 recv
    //      - recv 返回 0 表示对端关闭，return false；返回 SOCKET_ERROR 抛 runtime_error（带 WSAGetLastError()）
    //      - 找到后：headEnd = pos + 4
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
    // ② 请求行：buf[0, headEnd) 的第一行形如 "GET /path HTTP/1.1\r"
    //    按空格切成三段，写进 req.method / req.path / req.version（版本那段去掉行尾 \r）。
    //    切不出三段 → 抛 std::runtime_error("请求行格式错误")
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

    // ③ 剩下的行逐行拆 header，直到空行为止：
    //    每行形如 "Host: 127.0.0.1\r"，按第一个 ':' 切成键和值；
    //    键原样存，值要去掉前导空格和行尾 \r（"Key:   值" 里可能有多个空格）。
    //    没有 ':' 的行跳过（防御性，不抛）。
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

    // ④ 读 body：先查 Content-Length（findHeader(req, "content-length")，没有就当 0），转成数字。
    //    buf 里 [headEnd, buf.size()) 已经是 body 的开头部分，先拿过来；
    //    不够长就继续 recv 追加到 req.body，直到够。
    //    recv 返回 0 = 对端提前关闭 → return false；Content-Length 不是数字 → 抛异常。
    std::size_t need = 0;                       // 默认没有 body
    const std::string* cl = findHeader(req, "content-length");

    if (cl != nullptr) {
        need = std::stoul(*cl);                 // 不是数字时 stoul 自己抛异常，正合注释要求
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
