# mini_xray 调用关系图

> 生成于 M4（T4.4）收口时。箭头方向 = **调用 / include 的方向**（A → B 表示 A 用到了 B）。
> 实线 = 运行时主链；虚线 = 离线 / 教学层 / 尚未接入主链。

## 一、目录层依赖

```mermaid
flowchart TB
    main["src/main.cpp"] --> cli["src/cli/"]
    cli --> config["src/config/"]
    cli --> proxy["src/proxy/"]
    cli --> store["src/store/"]
    cli --> parse["src/parse/"]

    proxy --> config
    proxy --> store
    proxy --> common["src/common/"]
    proxy --> third["third_party/<br/>httplib + nlohmann"]

    store --> common
    store --> parse
    store --> events["runs/&lt;stamp&gt;/events.jsonl"]
    cli --> events

    net["src/net/<br/>src/http/"] -. 教学层，主链未接入 .-> third
    parse -. chunked 离线解析，主链未接入 .-> third
    tests["tests/*_test.cpp"] -. 各关验证台 .-> proxy
    tests -.-> store
    tests -.-> parse
    tests -.-> net
```

**读法**：`cli/` 是总入口，`proxy/` 是转发核心，`store/` 管落盘，`parse/` 管字节/文本解析。
`net/` 与 `http/` 是 M2 手写 HTTP 的教学产物，被 M3 起的 httplib 主链取代，只由验证台使用；
`parse/chunked.cpp` 同理（httplib 客户端侧已解 chunked），它的舞台在 M5 离线解析。

## 二、运行时主链（`mxr serve`）

```mermaid
flowchart LR
    main["main.cpp"] --> cli["cli.cpp<br/>run(argc,argv)"]
    cli -->|serve| serve["proxy/server.cpp<br/>serve(cfg)"]
    cli -->|cfg| cfg["config/config.cpp<br/>loadConfig"]
    cfg -->|校验| route["proxy/route.cpp<br/>validateUpstreams"]
    cli -->|echo| jw["store/jsonl_writer.cpp"]

    serve --> rec["store/recorder.cpp<br/>Recorder（写线程）"]
    serve --> resolve["proxy/route.cpp<br/>resolve"]
    serve --> target["proxy/target.cpp<br/>buildTarget"]
    serve --> handler["handler<br/>三段式 req/resp"]
    handler --> forward["forwardStreaming"]
    forward --> bridge["proxy/stream_bridge.cpp<br/>StreamBridge"]
    forward --> httplib["httplib::Client<br/>上游线程"]

    httplib -->|content_receiver| bridge
    bridge -->|next| provider["chunked provider<br/>httplib 工作线程"]
    provider --> sink["DataSink.write → 客户端"]

    handler -->|push Event| rec
    rec --> redact["store/redact.cpp"]
    rec --> ev["store/event.cpp<br/>toJson"]
    redact --> ev
    ev --> jw2["store/jsonl_writer.cpp"]
    jw2 --> fw["common/filewriter.cpp"]
    fw --> file["events.jsonl"]
```

**两条线程、一座桥**：上游线程调 `push()`，httplib 工作线程调 `next()`，`StreamBridge` 的有界队列在中间做背压。
handler 只等响应头就返回，之后客户端看到的每一块都由 provider 从桥上取。

## 三、M4 的在线 / 离线闭环（T4.3 + T4.4）

```mermaid
flowchart TB
    subgraph online["在线：一次 SSE 请求"]
        chunk["provider 收到一块原始字节"] --> onChunk["SseRunLogger::onChunk"]
        onChunk -->|feed| acc1["SseAccumulator<br/>（在线累积器）"]
        onChunk -->|push sse_chunk| rec2["Recorder → events.jsonl"]
        done["流结束 / 客户端断开"] --> onEnd["SseRunLogger::onEnd"]
        acc1 --> onEnd
        onEnd -->|text / usage / 耗时 / 坏块数| rec2
    end

    rec2 --> file2["events.jsonl"]

    subgraph offline["离线：mxr replay"]
        file2 -->|只读逐行| rp["cli/replay.cpp<br/>replayFile"]
        rp -->|sse_chunk.body| acc2["SseAccumulator<br/>（重放累积器，新建）"]
        acc2 --> cmp["重建 text / usage"]
        rp -->|sse_end.text| cmp
        cmp --> verdict{"一致？"}
        verdict -->|是| ok["退出码 0"]
        verdict -->|否| bad["mismatch +1"]
        verdict -->|缺 sse_end| inc["incomplete +1"]
    end

    cli2["cli.cpp → replay"] --> rp
```

**为什么对账成立**：两边用的是**同一个** `parse/sse.cpp` 的 `SseAccumulator`；
在线喂的是收到的字节，离线喂的是文件里的 `sse_chunk.body`。
如果落盘时字节被改了、块丢了、顺序乱了，重建结果就会和 `sse_end` 不一致。

## 四、重要 cpp 一览（谁负责什么）

| 文件 | 职责 | 被谁调 |
|---|---|---|
| `src/main.cpp` | 只做一次转调 | — |
| `src/cli/cli.cpp` | 子命令分派、全局 flag、退出码 | main |
| `src/cli/replay.cpp` | 离线重放与对账，只读 | cli（`replay`） |
| `src/config/config.cpp` | 读配置、`Config` 结构 | cli、route/target/server |
| `src/proxy/route.cpp` | 路径段前缀匹配、剥前缀、启动期校验 | cli、server |
| `src/proxy/target.cpp` | 拆 origin / 拼目标 URL | server |
| `src/proxy/server.cpp` | handler 三段式、转发两种模式、Ctrl+C 优雅退出 | cli |
| `src/proxy/stream_bridge.cpp` | 跨线程有界字节队列 + 背压 | server |
| `src/store/recorder.cpp` | 异步写线程、脱敏、截断、seq 分配 | server |
| `src/store/event.cpp` | 事件 → JSON 的稳定 schema | recorder |
| `src/store/redact.cpp` | 落盘前脱敏（正则） | recorder |
| `src/store/jsonl_writer.cpp` / `common/filewriter.cpp` | 整行追加 + flush / 文件句柄 | recorder、cli（echo） |
| `src/store/sse_run.cpp` | 一条流的在线记录员（累积器 + 序号 + 收尾） | server（provider） |
| `src/parse/sse.cpp` | SSE 两级缓冲状态机、字段提取 | sse_run（在线）、replay（离线） |
| `src/parse/chunked.cpp` | chunked 分帧解码 | 目前只有验证台；M5 离线解析会接 |
| `src/common/clock.cpp` | `nowMs()`、`stampNow()` | server、sse_run |
| `src/net/*`、`src/http/*` | M2 手写 socket / HTTP 解析 | 只有验证台 |

## 五、一张图记住边界

```
客户端 ──HTTP──> [proxy/server.cpp] ──HTTP──> 上游
                      │  │
       跨线程字节队列 ─┘  └─ 事件队列 ──> [store] ──> events.jsonl
       (StreamBridge)      (Recorder)          │
                                                └──> [cli/replay.cpp] 只读重建
```

三层缓冲各管一段：**TCP/HTTP 分帧**（httplib 内部）、**跨线程队列**（StreamBridge）、
**SSE 两级缓冲**（SseAccumulator）。回答"一个字节经过了哪几层"时，按这三层从上往下数即可。
