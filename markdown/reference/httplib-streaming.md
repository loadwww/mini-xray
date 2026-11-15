# httplib 流式 API（v0.15.3）

> 用于 T4.2。这一关要"边收边发"，而 `Client::send(const Request&)` 是**整包收完才返回**的。
> 下面这些是把"收"和"发"拆成两个方向的全部接口。

## 一、客户端侧：怎么"一块一块地收"

v0.15.3 **没有** `Post(path, headers, body, ContentReceiver)` 这种重载（只有 `Get` 系列有）。
流式接收靠给 `Request` 挂两个字段，然后照常 `send`。

```cpp
// <httplib.h:438>
using ResponseHandler = std::function<bool(const Response &response)>;

// <httplib.h:498>
using ContentReceiverWithProgress =
    std::function<bool(const char *data, size_t data_length,
                       uint64_t offset, uint64_t total_length)>;
```

`httplib::Request` 里的两个成员（<httplib.h:555-556>）：

```cpp
struct Request {
    ...
    ResponseHandler response_handler;              // 头到齐时回调（body 之前）
    ContentReceiverWithProgress content_receiver;  // 每收到一块 body 回调一次
};
```

调用顺序（`ClientImpl::process_request`）：

```
读状态行 + header  ──> 有 ? response_handler(res)  : 跳过
                        │  返回 false → 中断（Error::Canceled）
                        ▼
读 body  ──> 每块都调 content_receiver(data, len, offset, total)
             │ 返回 false → 中断（Error::Canceled）
             │ 没有设 content_receiver → 全部 append 进 res.body（这就是 T3.2 的行为）
             ▼
send() 返回 Result
```

要点：

| 事实 | 后果 |
|---|---|
| `response_handler` 在 body 之前 | 这是**唯一**能提前拿到状态码和 Content-Type 的时机 |
| `content_receiver` 拿到的已解 chunked | 不用再跑一遍 `ChunkedDecoder`；T4.1 只在 M5 离线解析里有舞台 |
| 一块可能含多个 SSE 事件，也可能把一个事件切成两半 | 下游必须自己按 `\n\n` 重新分帧（T4.3） |
| 返回 false = 取消 | 这是"客户端断开后让上游停下"的唯一钩子 |

## 二、服务端侧：怎么"一块一块地发"

```cpp
// <httplib.h:613>
void Response::set_chunked_content_provider(
    const std::string &content_type,
    ContentProviderWithoutLength provider,                  // 主回调
    ContentProviderResourceReleaser resource_releaser = nullptr);

// <httplib.h:486>
using ContentProviderWithoutLength = std::function<bool(size_t offset, DataSink &sink)>;
```

`DataSink`（<httplib.h:462> 起）：

```cpp
class DataSink {
public:
    std::function<bool(const char *data, size_t data_len)> write;  // 写一块；失败返回 false
    std::function<bool()> is_writable;                            // 客户端还连着吗
    std::function<void()> done;                                   // 说完了（写 0\r\n\r\n）
    std::function<void(const Headers &trailer)> done_with_trailer;
    std::ostream os;                                              // 也能当流用
};
```

库怎么调 provider（`write_content_chunked`，<httplib.h:4144> 起）：

```cpp
bool data_available = true;
while (data_available && !is_shutting_down()) {
    if (!strm.is_writable())          { error = Error::Write;    return false; }
    if (!provider(offset, sink))      { error = Error::Canceled; return false; }
    // data_available 在 sink.write() 里被置为 (len > 0)
}
```

三条从这段代码直接读出来的规矩：

1. **写完最后一块必须 `sink.done()`**。它把 `data_available` 置 false，循环才会退出并补上 `0\r\n\r\n`。只 `return true` 不调 `done()` → 库认为"你还没说完"，立刻再调一次 → 100% CPU 空转。
2. **`provider` 返回 false = 断开连接**（`Error::Canceled`）。别用它表示"暂时没数据"，没数据就在里面阻塞等。
3. **`sink.write` 返回 false 表示写失败**（客户端走了），原样 return 出去即可。

## 三、超时

```cpp
cli.set_connection_timeout(3, 0);   // 秒, 微秒 —— 连不上最多等 3s
cli.set_read_timeout(300, 0);       // 两个字节之间最多等 300s（默认只有 5s！）
cli.set_write_timeout(300, 0);

svr.set_read_timeout(300, 0);       // 服务端同理
svr.set_write_timeout(300, 0);
```

`read_timeout` 是**两个字节之间**的间隔，不是整条流的总时长。默认 `CPPHTTPLIB_READ_TIMEOUT_SECOND = 5`，SSE 里上游思考 6 秒就会被库自己掐断，报的是 `Error::Read`（看起来像上游挂了）。

## 四、这一关用不到但值得知道的

| API | 干什么 | 为什么不用 |
|---|---|---|
| `Client::Get(path, ContentReceiver)` | GET 版流式接收 | 我们发的是 POST |
| `Client::Get(path, ResponseHandler, ContentReceiver)` | 头 + body 都流式 | 同上 |
| `Response::set_content_provider(ct, ContentProvider)` | 带总长度的 provider | 流式没有总长 |
| `DataSink::os` | 把 sink 当 `std::ostream` 用 | 直接 `write()` 更明确 |
| `Client::set_keep_alive(true)` | 连接复用 | 每请求一个 Client，M4 之后再说 |

## 五、常见错误的症状对照

| 写错的地方 | 症状 |
|---|---|
| provider 里流结束只 `return true`，不调 `sink.done()` | CPU 100%，客户端收不到结束标记，连接不关 |
| 用 `send()` 的默认行为（不设 `content_receiver`） | 客户端全程静默，最后一口气刷出全部内容 |
| `content_receiver` 一直返回 true，不理会下游关闭 | 客户端断开后上游线程继续跑到流结束（浪费上游配额） |
| 队列满了丢数据（照抄 Recorder 的策略） | 客户端收到的 JSON 残缺，解析随机失败 |
| 队列无界 | 客户端断网后内存持续增长 |
| 忘了 `set_read_timeout` | 上游两块之间超过 5 秒就报 `Error::Read` |
