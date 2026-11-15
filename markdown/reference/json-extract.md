# JSON 协议字段安全提取速查（T5.2）

> 从"别人给的 JSON"里取字段，和从"自己序列化出来的 JSON"里读回来是两回事：
> 前者字段可能缺、类型可能错、甚至整个串都不是 JSON。这一关的纪律是
> **不抛异常 + 坏字段计数**。

## 一、不抛异常的解析

```cpp
#include <nlohmann/json.hpp>

auto j = nlohmann::json::parse(text, nullptr, false);
//                                  ↑        ↑
//                            无自定义回调  失败时不抛
if (j.is_discarded()) { /* 整串坏掉，取全默认值 */ }
```

`parse` 的三参数形式返回 `discarded` 而不是抛 `parse_error`。
`std::string` 里的 `\u0000`、截断的 UTF-8、半个 JSON 都会走这条路。

## 二、缺键 / 类型不符 / null 三者语义不同

| 情况 | 处理 | 算不算 parse_failed |
|---|---|---|
| 键不存在 | 取默认值 | **不算**（协议允许省略） |
| 键存在但是 `null` | 取默认值（OpenAI 的 `finish_reason:null` 是合法值） | **不算** |
| 键存在但类型不对 | 取默认值 | **算一次** |
| 整串不是 JSON | 全默认值 | 算一次 |

```cpp
if (!j.is_object() || !j.contains(key)) return fallback;
const auto &v = j.at(key);
if (v.is_null()) return fallback;
if (v.is_string()) return v.get<std::string>();
++failed; return fallback;
```

常用类型判断：`is_object()` / `is_array()` / `is_string()` / `is_number()`
/ `is_boolean()` / `is_null()` / `is_discarded()`。

`is_number()` 覆盖 integer / unsigned / float；`get<long long>()` 对 float 会截断。

## 三、OpenAI 字段位置（最容易记错的一张表）

| 字段 | 请求 body | 非流式响应 body | sse_end 事件 |
|---|---|---|---|
| model | `model` | — | — |
| stream | `stream` | — | — |
| messages | `messages[]`（role/content/tool_call_id/name） | — | — |
| tools | `tools[].function.name` | — | — |
| 输出文本 | — | `choices[0].message.content` | `text`（M4 已累积 delta） |
| 工具调用 | — | `choices[0].message.tool_calls[].{id, function.name, function.arguments}` | 本关留空（T5.4） |
| finish_reason | — | `choices[0].finish_reason`（**不在 message 里**） | `finish_reason` |
| usage | — | **顶层**，和 choices 平级 | `usage`（对象） |

三个反复踩的坑：

1. `tool_calls` 在 `message` **里面**，`finish_reason` 在 `message` **外面**。
2. `usage` 是**顶层**，不在 choices 里；流式用法里常要靠 `stream_options.include_usage` 才有。
3. `choices` 为空时**仍要继续读 usage**，别提前 return 把顶层用量漏掉。

## 四、`Usage::observed` 的判据

```cpp
Usage u;                              // observed 默认 false
if (!j.contains("usage")) return u;   // 未观测：三个数都是 0
const auto &v = j.at("usage");
if (!v.is_object()) { ++failed; return u; }
u.observed = true;                    // 观测到了，哪怕三个数真是 0
u.promptTokens = ...;
```

- `observed=false, total=0` → 上游没给，不能当成"零消耗"
- `observed=true, total=0` → 上游明确给了 0
- M6 做成本/耗时统计时把两者混为一谈，均值会被拉低

## 五、为什么流式和非流式要产出同一个结构

```cpp
ResponseInfo a = parseResponse(respBody);   // 非流式
ResponseInfo b = parseSseEnd(sseEndEvent);  // 流式（M4 累积后的结论）
// 逐字段比：outputText / finishReason / usage
```

T5.3 建链不该关心这次请求是不是 `stream`，否则每条判断都要写两遍。
**"同一个输出类型"就是一致性验收的结构保证。**

## 六、常见错误的症状对照

| 写错的地方 | 症状 |
|---|---|
| `parse(text)` 用会抛的版本 | 一行坏数据让整个 rebuild 崩掉 |
| 用 `j["key"]` 判存在 | 非 const json 的下标会**插入 null**，把"缺键"变成"有键" |
| `null` 当成 parse_failed | 正常的 `finish_reason:null` 被误报成坏数据 |
| 把 `tool_calls` 写在顶层找 | 永远取不到工具调用 |
| `choices` 空就 return | 漏掉顶层 usage，observed 变成 false |
| 流式路径自己再跑一遍 delta 累积 | 和 M4 的 sse_end 重复劳动，还可能算出不一致的结果 |
| 只判 `contains` 不判类型 | `"model":42` 让 `get<std::string>()` 抛异常 |
| 忘了计 `parseFailed` | 坏数据被静默吞掉，事后不知道文件有没有问题 |
