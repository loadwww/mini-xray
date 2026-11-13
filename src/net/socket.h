#pragma once

// winsock2.h 必须出现在 windows.h 之前。本项目目前没有包含 windows.h，
// 这行单独放在最上面是为了把这条规矩钉在这里，将来谁加了 windows.h 也不会踩雷。
#include <winsock2.h>

#include <string>
#include <string_view>

namespace mxr {

// WsaGuard：进程级 Winsock 的"开关"。构造 = 载入 ws2_32.dll 并协商版本，析构 = 卸载。
// 用法：在 run() 里构造一个局部变量，让它活到进程结束（它必须是整个进程里最外层的那一个）。
// 为什么不让 Socket 自己 WSAStartup：那是"进程级"的一次性初始化，塞进"每个连接对象"里语义就错了
// —— 第一个 Socket 负责初始化、最后一个析构的又要负责清理，等于把进程级状态藏进了对象计数里。
class WsaGuard {
public:
    WsaGuard();                                 // 失败 → 抛 std::runtime_error
    ~WsaGuard();                                // WSACleanup()

    WsaGuard(const WsaGuard&) = delete;
    WsaGuard& operator=(const WsaGuard&) = delete;
};

// Socket：一条 TCP 客户端连接。职责只有"搬字节"——连上去、把一段字节全发出去、来多少收多少。
// 它不认识 HTTP，也不判断报文边界（找 \r\n\r\n、算 Content-Length 属于 M3 的解析层）。
// 两步走：构造只创建句柄（本地操作，不会因网络失败），connectTo() 才真正连接（可能阻塞十几秒）。
class Socket {
public:
    Socket();                                   // 客户端：只创建句柄，失败抛 std::runtime_error
    ~Socket();                                  // 句柄有效才 closesocket

    // 服务端：接管一个 accept() 出来的、已经连上的句柄，不经过 connect。
    // 为什么不直接开一个公开的 setHandle：那等于让"句柄从哪来"变成随时可改的状态，
    // 谁都能把别人的句柄塞进来，析构时 double close。adopt 是一次性、具名的入口，语义明确。
    static Socket adopt(SOCKET handle);

    Socket(const Socket&) = delete;             // 一个句柄只能有一个主人
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;            // 移交句柄，源变 INVALID_SOCKET
    Socket& operator=(Socket&& other) noexcept;

    void connectTo(const std::string& host, int port);  // 失败抛 std::runtime_error
    void sendAll(std::string_view data);                // 全部发出，否则抛异常
    int  recvSome(char* buf, int len);                  // 透传 recv：>0 字节数 / 0 对端关闭 / SOCKET_ERROR

    bool isValid() const { return handle_ != INVALID_SOCKET; }

private:
    struct AdoptTag {};                         // 只为把"接管现成句柄"和"新建句柄"两个构造函数区分开
    Socket(SOCKET handle, AdoptTag);            // 只有 adopt() 会用到

    SOCKET handle_ = INVALID_SOCKET;
};

} // namespace mxr
