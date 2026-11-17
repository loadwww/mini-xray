# 接口速查（reference）

这里**只放函数/类型原型**，不放实现。写代码时卡在"这个函数怎么调"就翻这里，不用翻聊天记录。

## 约定

- 每个任务开工前，本目录补一份对应的原型清单
- 文件名按主题，不按任务号（一个主题可能跨多个任务被反复用）
- 原型来自标准库文档 / 第三方库文档，**不是本项目代码**

## 索引

| 文件 | 内容 | 首次用到 |
|---|---|---|
| [fstream.md](fstream.md) | 文件读写流：`ofstream` / `ifstream` / 打开模式 / 状态位 | T1.2 |
| [nlohmann-json.md](nlohmann-json.md) | JSON 解析、取值、遍历、异常、序列化 | T1.3 |
| [optional.md](optional.md) | `std::optional<T>` 的完整接口 | T1.3 |
| [mutex-thread.md](mutex-thread.md) | `std::mutex` / `lock_guard` / `unique_lock` / `std::thread`，以及"什么不是线程安全的" | T1.4 |
| [winsock.md](winsock.md) | Winsock2：`WSAStartup` / `socket` / `connect` / `send` / `recv` 的返回值语义与错误码 | T2.1 |
| [httplib.md](httplib.md) | cpp-httplib：`Server` / `Request` / `Response` / 路由注册 / 与手写版的对照表 | T2.3 |
| [std-string.md](std-string.md) | `std::string`：`compare` 判前缀、`substr`、`insert`、`npos`，以及无符号下溢/`operator[]` 越界两个坑 | T3.1 |
| [condition-variable.md](condition-variable.md) | `std::thread` / `condition_variable` / `unique_lock` / `queue` / `atomic` / `<filesystem>`，含生产者-消费者模板 | T3.3 |
| [string-view.md](string-view.md) | `std::string_view`：成员函数、三个坑（悬垂/无 `\0`/隐式转换方向）、何时用何时不用；附 `<cctype>` 字符分类与十六进制转换 | T4.1 |
| [httplib-streaming.md](httplib-streaming.md) | 流式 API：`Request::content_receiver` / `response_handler`、`set_chunked_content_provider`、`DataSink`、provider 的调用循环语义 | T4.2 |
| [sse.md](sse.md) | SSE 行规则、OpenAI chunk 字段位置、nlohmann 安全提取套路、`std::string` 切行原型 | T4.3 |
| [replay-cli.md](replay-cli.md) | `ifstream`/`getline`、不抛异常的 `json::parse`、`value<T>` 默认值类型、两个钟的分工、本项目 Event/Recorder/Replay 接口 | T4.4 |
| [nlohmann-adl.md](nlohmann-adl.md) | ADL `to_json`/`from_json`、`json j = obj` / `j.get<T>()`、`value` vs `at` vs `get`、枚举字符串化、C++17 手写 `operator==` | T5.1 |
| [json-extract.md](json-extract.md) | 不抛异常的 `parse`、`is_xxx` 类型判断、缺键/null/类型不符三者语义、OpenAI 字段位置表、`Usage::observed` 判据 | T5.2 |
| [chain-hash.md](chain-hash.md) | 滚动前缀哈希与 FNV-1a、前缀判定公式、稳定 run id、`map`/`sort`/`getline` 配对原型、system-only 陷阱 | T5.3 |
| [superstep.md](superstep.md) | 超步归并：ToolExec/ToolCall 字段表、`contains` 只读判断、`map` 反查、`max` 钳负区间、失败分级判定顺序 | T5.4 |
| [mermaid.md](mermaid.md) | Mermaid flowchart/gantt 语法、HTML 实体转义表、id 与标签分离、字符串拼接原型 | T6.1 |
| [finalize.md](finalize.md) | filesystem 路径/原子写/幂等/rename 语义、meta 降级计数来源、sessionId 安全约束 | T6.2 |
| [markdown-table.md](markdown-table.md) | 表格语法、`|`/反引号/换行转义、UTF-8 截断边界、空值 `—`、失败信息前置 | T6.3 |

## 记一条纪律

原型只是"能调什么"，**怎么组织**仍然要自己想。抄原型写代码没问题，抄别人的结构不行。
