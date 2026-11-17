# 落盘 finalize 原型速查（T6.2）

只放原型，不放实现。

## std::filesystem（C++17）

```cpp
#include <filesystem>
namespace fs = std::filesystem;

fs::path dir = fs::path(root) / sessionId;   // 跨平台拼路径，不用 "/" 手拼
bool ok = fs::create_directories(dir);       // 已存在返回 false（不是错误）
bool exists = fs::exists(p);                 // 存在性
bool isFile = fs::is_regular_file(p);
std::uintmax_t n = fs::file_size(p);         // 坏路径会抛 filesystem_error

bool renamed = fs::rename(tmp, final);       // 同盘原子重命名；跨盘会抛
bool removed = fs::remove(p);                // 删文件/空目录
```

关键语义：
- **原子写**：内容先写 `<name>.tmp` → 写完 flush/关闭 → `rename(tmp, final)`。
  读者要么看到旧文件、要么看到完整新文件，永远看不到半截。
- **为什么不能直接覆盖**：`ofstream(final)` 会先截断为 0 字节再写入；
  中途 kill / 磁盘满 → 留下半截 JSON，重放直接崩。
- **rename 的前提**：tmp 与 final 必须在**同一目录/同一磁盘**，否则不是原子操作。
- Windows 上 `rename` 遇到目标已存在会**失败**（POSIX 会覆盖）——
  幂等场景要么先 `remove(final)`，要么用 `rename` 的 error_code 重载判断。

## error_code 重载（不抛版本）

```cpp
std::error_code ec;
fs::rename(tmp, final, ec);          // 失败写 ec，不抛
if (ec) { /* 用 ec.message() 报告 */ }
```

## 字符串 → 文件的原子写助手（本关要写的核心）

```cpp
bool atomicWrite(const fs::path &finalPath, const std::string &content)
{
    fs::path tmp = finalPath;
    tmp += ".tmp";                           // 同目录，保证原子 rename
    // 1. ofstream 写 tmp
    // 2. flush + close（析构或显式 close）
    // 3. 若 final 已存在先 remove（Windows）
    // 4. rename(tmp, final)
    // 失败要清理 tmp，别留垃圾
}
```

## 幂等怎么做

同一个 sessionId 重复 finalize：
- 每次都重新生成全部文件（内容一致）→ 结果幂等
- 但不能产生 `run(1).json` 这类副本 → 文件名固定，覆盖式原子写
- `meta.json` 里的时间字段：若用 `nowMs()`，重复跑字节会变——
  用**事件里的时间戳**（run.startMs / endMs）当会话时间，保证字节一致

## meta.json 降级计数来自哪里

| 字段 | 来源 |
|---|---|
| `parse_failed` | T5.2 的 `RequestInfo::parseFailed` / `ResponseInfo::parseFailed` 累加 |
| `dropped_events` | T5.3 `PairResult::orphanResponses` + readEvents 跳过的坏行数 |

这些数字是**数据质量指标**：>0 说明这份报告是基于不完整输入得出的，
使用者需要知道"结论的可信度打了折扣"。

## 本关函数（src/store/finalize.h）

```cpp
struct FinalizeResult {
    bool ok = false;
    std::string error;              // 失败原因（空 = 成功）
    int filesWritten = 0;
};

struct FinalizeInput {
    std::string sessionId;
    std::vector<Run> runs;
    std::string eventsJsonl;        // 原样保留的采集文件（已经落盘则传路径）
    std::string graphMmd;
    std::string timelineMmd;
    int parseFailed = 0;
    int droppedEvents = 0;
};

FinalizeResult finalize(const std::string &rootDir, const FinalizeInput &in);
```

## 判据

- 一次 finalize 产出 5 类文件，内容互相自洽（meta 的步数 == run.json 的 steps 数）
- 中途 kill 不留半截 json（原子写保证）
- 重复跑两次，meta.json 字节一致（时间字段除外——本关用事件时间，所以字节一致）
- 目录名只接受 `[A-Za-z0-9_-]`，拒绝 `..` / 绝对路径（路径穿越防护，与 T7.1 同源）
