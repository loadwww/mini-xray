# cpp-httplib 原型速查（v0.15.3，header-only）

单个头文件 `third_party/cpp-httplib/httplib.h`，编译时 include 即可，
不需要链接额外库（Windows 下仍要 `ws2_32`，CMake 里已加）。
头文件很大（约 31 万字符），**只在一个 .cpp 里 include**，别放进自己的头文件。

## 服务端

```cpp
#include <httplib.h>

httplib::Server svr;                                  // 一个 server = 监听器 + 线程池 + 解析器

svr.Get("/hello", handler);                           // 注册路由（还有 Post / Put / Delete ...）
svr.Post("/v1/chat/completions", handler);

bool ok = svr.listen("0.0.0.0", 9000);                // 阻塞！直到 stop() 或出错，返回是否正常结束
svr.stop();                                           // 让 listen 返回（一般从别的线程调）
```

## handler 的签名

```cpp
void handler(const httplib::Request& req, httplib::Response& res);
//  req：只读 —— 请求已经解析好了，字段直接用
//  res：可写   —— 往里填响应，listen 的循环负责发出去
```

## Request（只读）常用成员

```cpp
req.method        // "GET" / "POST"（字符串）
req.path          // 已解码的路径，"/hello"
req.version       // "HTTP/1.1"
req.body          // 请求体，std::string
req.headers       // 类 map：req.headers.find("Content-Length")、
                  //   键已被库归一成小写，查什么都要用小写
req.get_header_value("Content-Type")                  // 找不到返回 ""，最省事
req.params         // ?a=1&b=2 解析结果（仅 GET 的 query string）
req.matches        // 路由里的正则捕获组（pattern 带正则时才有内容）
```

## Response（可写）常用成员

```cpp
res.status = 200;                                     // 不设默认 200
res.set_content(std::string("正文"), "text/plain");   // 正文 + Content-Type，一次给全
res.set_header("X-Custom", "v");                      // 额外的响应头
res.body = "...";                                     // 等价于 set_content 的低配版
```

## 多线程注意

handler 在**线程池的工作线程**里执行，同一个 server 可能有多个 handler 并发跑。
handler 里若写共享数据（计数器、你的 JsonlWriter），要么加锁，要么用无锁的原子量。

## 和手写版的对应关系（本关要列的清单线索）

| 手写的（T2.1/T2.2） | httplib 里的对应物 |
|---|---|
| WsaGuard | 库内部在 listen 时自己做 WSAStartup |
| Listener（bind/listen） | `svr.listen(host, port)` |
| accept 循环 + 每连接一个处理 | 内置线程池，每个连接丢给一个工作线程 |
| readRequest 找 `\r\n\r\n` | 库内解析器：请求行/headers/body 全拆好 |
| Content-Length 拼凑 body | 同上，还支持 chunked 编码 |
| keep-alive（手写版没有：一连接一请求） | 库默认支持，一个连接连发多个请求 |
| findHeader 的大小写不敏感查找 | 库把所有键归一成小写 |
| 手写版没有：URL 解码 | `req.path` 已解码 |
| 手写版没有：超时 | 库可设读写超时（T3.2 起会用到） |

## 客户端（T3.2 起）

```cpp
#include <httplib.h>

httplib::Client cli("http://127.0.0.1:9100");   // scheme://host:port
httplib::Client cli("127.0.0.1", 9100);         // 等价写法，只能 http

cli.set_connection_timeout(3, 0);               // 连接超时：秒, 微秒
cli.set_read_timeout(30, 0);                    // 读超时：上游接了连接但不回数据
cli.set_write_timeout(30, 0);
```

### 通用发送（代理就用这个）

```cpp
httplib::Request req;              // 自己造一个，不是收到的那个
req.method = "POST";
req.path   = "/v1/chat/completions";
req.body   = body;
req.set_header("Content-Type", "application/json");

httplib::Result r = cli.send(req);
```

不需要为每个 method 记一个函数（`Get` / `Post` / `Put` … 那些重载都在，但代理用 `send` 更直接）。

### `Result`：response 或 error，二选一

```cpp
class Result {
    operator bool() const;                 // true = 拿到了响应
    Response& operator*();                 // 解引用（未检查）
    Response* operator->();                // r->status / r->body
    Error error() const;                   // 失败原因（枚举）
};

std::string to_string(Error);              // 枚举转人话："Connection" / "Timeout" / ...
```

用法：

```cpp
if (!r) {
    // 连不上、DNS 失败、超时、协议错 —— 都属于"网关拿不到上游响应"
    std::cerr << to_string(r.error());
} else {
    r->status;                              // 上游的状态码，可能是 200/400/500
    r->body;                                // 上游的正文
    r->get_header_value("Content-Type");     // 头（键大小写不敏感，找不到返回 ""）
}
```

### 三个坑

1. **`Result` 里有 `unique_ptr<Response>`**，不可拷贝。`auto r = cli.send(req)` 之后只能 `r->`，不能赋值给别的变量再传。
2. **一个 Client 每次请求新建 = 每次 TCP 握手**（https 还要加 TLS 握手）。T3.2 先这样，M4 讨论连接复用。
3. **它不会替你处理 header**：`Host` 按 `scheme_host_port` 填，`Content-Length` 按实际 body 算，但 `Connection` / `Transfer-Encoding` 这些 hop-by-hop header 你传什么它就发什么——得自己挑。
