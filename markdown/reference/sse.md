# SSE 协议速查 + nlohmann 提取原型（T4.3）

> SSE（Server-Sent Events）：HTTP 长连接上服务器单向推文本帧。
> 整个协议的语法规则就四条，全在"行"这一层。

## 一、SSE 行规则（W3C 规范要点）

| 行的样子 | 含义 | 处理 |
|---|---|---|
| `data: <内容>` | 事件数据（核心） | 剥前缀，累积进当前事件 |
| （空行） | **事件边界** | 把累积的 data 交出去 |
| `: <任意>` | 注释（keep-alive 心跳） | 整行忽略 |
| `event: <名>` / `id: <n>` / `retry: <ms>` | 事件元信息 | mini 版忽略 |

关键细节：

1. **`data:` 后的空格是惯例不是语法**。`data:x` 和 `data: x` 等价；
   剥前缀后只剥**一个**空格（data 内容本身允许以空格开头）。
2. **多行 data 用 `\n` 拼接成一个事件**：
   ```
   data: 第一行
   data: 第二行
   （空行）
   ```
   交付给解析层的内容是 `"第一行\n第二行"`，不是两次独立事件。
3. **边界是空行，不是换行**。换行只负责切行，空行才负责"事件说完了"。
   所以必须有第二级缓冲（dataAcc_），不能见一行解析一行。
4. **事件可以没有 data**（只有注释行 + 空行）——这种"空事件"应当被忽略，
   不能计入坏块。
5. `[DONE]` **不是 SSE 规范**，是 OpenAI 的应用层约定：一行 `data: [DONE]`
   表示流语义上的结束。SSE 本身只认连接关闭。

## 二、OpenAI chunk 的 JSON 形状

```jsonc
// 内容块（大多数）
{"choices":[{"delta":{"content":"tok0 "},"finish_reason":null,"index":0}], ...}

// 最后一块：delta 空了、finish_reason 有值了、顶层多了 usage
{"choices":[{"delta":{},"finish_reason":"stop","index":0}],
 "usage":{"prompt_tokens":10,"completion_tokens":10,"total_tokens":20}}

// 结束标记
data: [DONE]
```

三个"有时有有时没有"的位置（提取前必须判断）：

| 字段 | 在哪 | 什么时候有 |
|---|---|---|
| `delta.content` | `choices[0].delta.content` | 内容块；最后一块是 `{}` |
| `finish_reason` | `choices[0].finish_reason` | 大多是 `null`，最后才是 `"stop"` |
| `usage` | 顶层（不在 choices 里！） | 最后一块；有的服务端要 `stream_options.include_usage` 才给 |

## 三、nlohmann 提取原型

```cpp
#include <nlohmann/json.hpp>

// 解析：失败抛 nlohmann::json::parse_error（json::exception 的子类）
nlohmann::json j = nlohmann::json::parse(str);
try { ... } catch (const nlohmann::json::exception &) { /* 坏块 */ }

// 存在性 / 类型判断（缺一样都可能抛异常）
j.contains("usage");                          // 键存在吗
j["choices"].is_array();
j["choices"].empty();
j["choices"][0]["delta"]["content"].is_string();
j["choices"][0]["finish_reason"].is_string(); // null → false，正好挡掉 null

// 取值
j["choices"][0]["delta"]["content"].get<std::string>();
j["usage"]["prompt_tokens"].get<long long>();
j.value("stream", false);                     // 带默认值取（旧相识）
```

安全提取的固定套路：

```cpp
if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
    const auto &c0 = j["choices"][0];
    if (c0.contains("delta") && c0["delta"].contains("content")
        && c0["delta"]["content"].is_string()) {
        text_ += c0["delta"]["content"].get<std::string>();
    }
}
```

## 四、std::string 切行原型

```cpp
s.find(c);              // 找字符，返回下标；找不到返回 std::string::npos
s.substr(pos, n);       // 取子串（不改 s）
s.erase(pos, n);        // 原地删除
s.rfind(prefix, 0)==0;  // 前缀判断（rfind 从下标 0 开始找，只可能命中前缀）
s.back(); s.pop_back(); // 末字符 / 删末字符
```

## 五、常见错误的症状对照

| 写错的地方 | 症状 |
|---|---|
| 见到 `\n` 就解析（没有 dataAcc_ 缓冲） | `data:` 行单独到就被当事件解析 → 大量坏块 |
| 没剥 `data:` 后的空格 | `json::parse(" {...}")` 能过（前导空白合法），但 `[DONE]` 判等失败 |
| `finish_reason` 直接 `get<std::string>()` | 大部分块它是 `null` → 抛异常 → 被当成坏块全计数 |
| 多行 data 之间没补 `\n` | 拼出的 JSON 形状错误，解析随机失败 |
| feed 里忘了先 append 再切 | 每块的第一个字符丢失 |
| 切行用 `for` 不用 `while` | 一次 feed 带多行时只处理第一行，其余滞留 |
| 没剥行尾 `\r` | JSON 末尾带 `\r` → parse 失败；`[DONE]\r` 判等失败 |
| `json::parse` 没 try/catch | 一个坏块直接整个进程崩 |
