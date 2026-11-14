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

## 记一条纪律

原型只是"能调什么"，**怎么组织**仍然要自己想。抄原型写代码没问题，抄别人的结构不行。
