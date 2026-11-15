# 重放 / 事件对账原型速查（T4.4）

> 这一关几乎不引入新库，但组合了前几关的四个接口：
> `ifstream` 读行、`nlohmann::json` 解析、`steady_clock` 计时、`Recorder` 入队。
> 卡在"这个函数怎么调"时按主题翻下面。

## 一、逐行读文件

```cpp
#include <fstream>

std::ifstream in(path);          // 默认 ios::in（文本模式）
if (!in) { /* 打不开 */ }         // 路径错、没权限、被占用都会是 false
std::string line;
while (std::getline(in, line)) { // 去掉 '\n'，不含它；最后一行没换行也能读到
    ...
}
```

注意：`std::getline` 返回的是流本身的引用，用 `while (getline(...))` 判的是
"这次读到东西了吗"，不要用 `while (!in.eof())`（会多处理一次空行）。

写回文件（测试里造样本用）：

```cpp
std::ofstream out(path);                    // 默认 ios::trunc（清空重写）
std::ofstream out(path, std::ios::app);     // 追加（JsonlWriter 用的就是 app）
```

## 二、不抛异常的 JSON 解析

```cpp
#include <nlohmann/json.hpp>

auto j = nlohmann::json::parse(line, nullptr, false);
//                                   ↑        ↑
//                             不用自定义回调  允许失败不抛
if (j.is_discarded()) { /* 坏行，跳过 */ }

const std::string type = j.value("type", "");     // 缺键返回默认值，不抛
const long long idx    = j.value("chunk_index", -1LL);  // ← 默认值要带 LL
long long total = 0;
if (j.contains("usage")) {
    total = j["usage"].value("total_tokens", 0LL);
}
```

**`value<T>(key, 默认值)` 的坑**：T 由默认值的类型推导。
写 `0` 推成 `int`、写 `0LL` 才推成 `long long`；用 `""` 推成 `const char*`
对 `std::string` 是安全的，但数字字段一定要对齐类型，否则抛 type_error。

序列化：

```cpp
j["usage"] = {{"prompt_tokens", 10LL},
              {"completion_tokens", 10LL},
              {"total_tokens", 20LL}};   // 嵌套 initializer_list → object
std::string line = j.dump();              // 一行文本（不含 '\n'）
```

## 三、计时：两个钟各管一件事

```cpp
#include <chrono>

auto t0 = std::chrono::steady_clock::now();
// ... 一段流 ...
long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - t0).count();
```

| 钟 | 用途 | 为什么 |
|---|---|---|
| `steady_clock` | 耗时 / 超时 | 单调不回退；系统时间被 NTP 改了也不会出现负耗时 |
| `system_clock` → `mxr::nowMs()` | `ts_ms` 事件时间 | 要能和别的系统日志按"几点"对上 |

## 四、本项目接口（不是标准库，但填空里要用）

```cpp
// store/recorder.h
void Recorder::push(Event ev);        // 入队即返回；seq 由它在锁内分配

// store/event.h
struct Event { std::string type, hop, method, path, upstream, body, text;
               long long seq, tsMs, chunkIndex, promptTokens, completionTokens,
               totalTokens, durationMs;
               int status, badDataCount; long long latencyMs;
               std::map<std::string,std::string> headers; };
nlohmann::json toJson(const Event&);  // 返回 json 对象，写盘时才 dump

// parse/sse.h
void SseAccumulator::feed(const std::string& bytes);
void SseAccumulator::finish();
bool done() const;  bool hasUsage() const;
const std::string& text() const;  const std::string& finishReason() const;
const SseUsage& usage() const;    // {promptTokens, completionTokens, totalTokens}
int badDataCount() const;  int eventCount() const;

// cli/replay.h
ReplaySummary replayFile(const std::string& path, std::ostream& os);
```

`acc = SseAccumulator();` 可以重置（成员都是 string / 整数 / bool，默认拷贝赋值可用）。

## 五、常见错误的症状对照

| 写错的地方 | 症状 |
|---|---|
| 用 `json::parse(line)` 没加 `, nullptr, false` | events.jsonl 里一行坏数据就让整个 replay 抛异常退出 |
| `value("chunk_index", -1)` 默认值写成 int | 数字类型不匹配，可能抛 type_error |
| 重放时喂 `text`（结论）而不是 `body`（原始字节） | 两边都不过解析器，对账全绿但什么都没验证 |
| 见到 sse_end 不重置累积器 | 第二条流的文本会接在第一条后面 |
| 用 `seq`（全局行号）当流内序号 | 中间夹着别的请求的 req/resp，必然跳号 |
| EOF 时不检查"还有没收尾的流" | 进程被杀留下的半截流被当成正常完成 |
| hop 变了不结算上一条流 | 多条流混在一起，incomplete 永远查不出来 |
| 把 `duration_ms` 记成 `system_clock` 之差 | 系统时间被改时出现负数或几小时的耗时 |
