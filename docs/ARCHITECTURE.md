# mini_xray 架构

`mxr` 是一个单进程 C++17 程序：录制 Agent ↔ OpenAI 兼容 API 的流量，再离线还原成执行图。设计原则是**每层只做一件事，跨层只传数据不传上下文**。

## 1. 模块边界

| 层 | 目录 | 负责 | 不负责 |
|---|---|---|---|
| 入口 | `main.cpp`、`cli/` | 子命令分派、退出码、定位验证台 | 业务逻辑 |
| 配置 | `config/` | 读 JSON、启动期校验上游 | 热重载、schema 演进 |
| 代理 | `proxy/` | 路径前缀路由、转发、SSE 字节桥、进程编排 | 协议字段语义 |
| 解析与还原 | `parse/`、`model/`、`chain/` | 字节/JSON → Run / Step / ToolExec | 文件 I/O、网络 |
| 落盘 | `store/` | 事件追加、脱敏、原子写 finalize | 展示与查询 |
| 展示 | `graph/`、`ui/` | Mermaid、Markdown 报告、HTML 路由 | 数据落盘 |

## 2. 数据流

```text
请求 → handler（记 req）→ resolve + buildTarget
     → 非流式整包转发；流式：上游线程 push → StreamBridge → provider → DataSink
     → 记 resp / sse_chunk / sse_end → Recorder 队列 → events.jsonl
     → finish：读 events → pairHops → buildRuns → mergeSupersteps → emitMermaid → finalize
     → 产物：events.jsonl / run*.json / graph.mmd / timeline.mmd / meta.json
```

`meta.json` 最后写，作为「这次重建完整」的完成标记；每个产物用同目录 `.tmp` + rename 原子落盘。

## 3. 线程模型

```text
listen 主线程 ─ 每连接一个 handler；Ctrl 事件只置 atomic 标志，stopWatcher 调 svr.stop()
上游接收线程 ─ content_receiver 把字节 push 进 StreamBridge（有界队列，满则阻塞 = 背压）
下游发送线程 ─ httplib 工作线程调 provider.next()，写给客户端 DataSink
Recorder 线程 ─ 单线程消费事件队列；析构时 flush 排空；队列满则丢弃并计数
```

跨线程传引用时只有一个原则：被引用对象的寿命必须盖过线程。`hop` 和 `fwdHeaders` 按值传给 provider，正是为了躲开 handler 返回后的悬垂引用。

## 4. 平台边界

| 边界 | Windows | Linux（CI 实跑） |
|---|---|---|
| 裸 Winsock / `net`、`http` 教学层 | 编译（验证台用） | CMake 不编译 |
| `run` 子进程编排 | `CreateProcessW` + 控制台事件 | 不支持，改用 `serve` + `finish` + POSIX signal |
| 链接 | `ws2_32` | `Threads::Threads` |
| e2e | `tests/e2e.ps1` | `tests/e2e.sh` |

更多调用级细节见 [`markdown/architecture.md`](../markdown/architecture.md)（四张 Mermaid 调用图与文件职责表）。
