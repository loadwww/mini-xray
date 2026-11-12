# Winsock2 原型（`<winsock2.h>` + `ws2_32.lib`）

## 开工前的三条硬规矩

1. **必须 `#include <winsock2.h>` 在 `<windows.h>` 之前**。否则 `windows.h` 会先拉进老的 `winsock.h`，两套定义冲突（一堆 `redefinition`）。
2. 链接要加 `ws2_32.lib`：CMake 里 `target_link_libraries(mxr PRIVATE ws2_32)`。
3. 用 Winsock 之前必须 `WSAStartup`，退出前 `WSACleanup`；不调 `WSAStartup` 所有 socket 函数直接返回 `SOCKET_ERROR`。

## 初始化 / 清理

```cpp
int WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData);   // 0 = 成功
int WSACleanup();                                              // 0 = 成功

WSADATA wsa;
if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { /* 失败 */ }
```

`MAKEWORD(2,2)` = 请求 Winsock 2.2。`WSADATA` 是出参，装版本信息，不关心也要传。

## 类型

```cpp
SOCKET          // 无符号整数句柄；无效值 INVALID_SOCKET（不是 -1，是 ~0）
SOCKET_ERROR    // = -1，函数失败时的返回值
sockaddr_in     // IPv4 地址结构
```

## 创建 / 连接

```cpp
SOCKET socket(int af, int type, int protocol);        // INVALID_SOCKET = 失败
//   af = AF_INET（IPv4）, type = SOCK_STREAM（TCP）, protocol = IPPROTO_TCP 或 0

int connect(SOCKET s, const sockaddr* name, int namelen);   // 0 = 成功，SOCKET_ERROR = 失败

// 填地址：sockaddr_in 三个字段
sockaddr_in addr{};
addr.sin_family      = AF_INET;                 // 必须
addr.sin_port        = htons(80);               // 主机字节序 → 网络字节序（大端）
addr.sin_addr.s_addr = inet_addr("127.0.0.1");  // 字符串 IP → 32 位网络序
// 或 inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
```

域名要解析时用 `getaddrinfo`：

```cpp
struct addrinfo hints{}, *res = nullptr;
hints.ai_family   = AF_INET;
hints.ai_socktype = SOCK_STREAM;
int rc = getaddrinfo("example.com", "80", &hints, &res);   // 0 = 成功
// res->ai_addr / res->ai_addrlen 直接喂给 connect
freeaddrinfo(res);
```

## 收发

```cpp
int send(SOCKET s, const char* buf, int len, int flags);    // 返回"实际发出"的字节数，可能 < len！
int recv(SOCKET s, char* buf, int len, int flags);          // 见下面三种返回值
```

`recv` 的返回值必须分三种处理：

| 返回值 | 含义 |
|---|---|
| `> 0` | 读到这么多字节，但**不保证一次读完**，要继续读 |
| `== 0` | 对端**正常关闭**（发了 FIN）。这是"读完了"的信号，不是错误 |
| `SOCKET_ERROR` | 出错，用 `WSAGetLastError()` 查原因 |

`send` 同理：可能只发出去一部分，要循环到全部发完。

## 关闭

```cpp
int shutdown(SOCKET s, int how);   // how = SD_SEND / SD_RECEIVE / SD_BOTH，先发 FIN
int closesocket(SOCKET s);         // 释放句柄，0 = 成功
```

RAII 就包在析构里调 `closesocket`；句柄无效时不要调（`INVALID_SOCKET` 要单独判）。

## 错误码

```cpp
int WSAGetLastError();     // 取本线程最近一次错误码
```

常见值：`10048` 端口已被占用（bind）、`10061` 连接被拒绝（对端没监听）、`10054` 连接被对端重置、`10060` 连接超时。查表：`FormatMessage` 或直接搜 "WSA error 10048"。

## 阻塞语义（本项目全程用阻塞 socket）

- `connect` 阻塞到三次握手完成或超时
- `recv` 阻塞到至少 1 字节到达、或对端关闭、或出错
- 没有数据又没有关闭 → **一直挂着**。所以读循环必须有明确的终止条件（`Connection: close` + `recv == 0`，或 `Content-Length` 读够）
