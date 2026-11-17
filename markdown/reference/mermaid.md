# Mermaid 文本图原型速查（T6.1）

这是**图语法 + 字符串拼接**的速查，不是本项目代码。

## Mermaid flowchart 语法（生成结果必须能被 mermaid.live 渲染）

```text
flowchart TD
    S0["① gpt-4o-mini · 120ms · 15 tok"]     ← 节点定义：id["标签"]
    T0_0["search · 120ms ~"]                  ← 工具子节点
    S0 --> T0_0                               ← 有向边
    S0 --> S1                                 ← 顺序边
    classDef failed fill:#f96,stroke:#333     ← 定义样式类
    class S0 failed                           ← 给节点套样式
```

硬约束（违反就渲染失败或破版）：
- 节点 id 只能含字母/数字/下划线；`s0`、`t0_1` 合法，`gpt-4o-mini` **不合法**（有 `-`）
- **id 与标签分离**：显示文字放 `["..."]` 里，id 用稳定短串——改名不影响边
- 标签里的特殊字符要转义（见下表），否则 `"` 会提前结束标签
- `classDef` 要先定义再 `class` 引用

## HTML 实体转义表（Mermaid 标签里用）

| 原字符 | 写成 | 原字符 | 写成 |
|---|---|---|---|
| `&` | `&amp;` | `<` | `&lt;` |
| `"` | `&quot;` | `>` | `&gt;` |
| 换行 `\n` | `<br/>` | `\r` | 直接丢掉 |

顺序坑：先转义 `&` 再处理别的，否则 `&quot;` 里的 `&` 会被二次转成 `&amp;quot;`。逐字符 switch 天然没有这个问题。

## Gantt 片段语法

```text
gantt
    title Execution timeline
    dateFormat x
    axisFormat %S
    section Step 0
    s0 :done, 1000, 1180
    search :done, 1180, 1300
```

- `dateFormat x` = 横轴用 Unix 毫秒整数；本项目的时间戳本来就是 `ts_ms`，直接填
- 每行格式：`标签 :状态, 开始毫秒, 结束毫秒`
- 状态可用 `done`（已完成）/ `active`（进行中）/ `crit`（关键）；本关按 `complete` 选

## C++ 字符串原型（只用标准库）

```cpp
std::string out;
out.reserve(128);            // 预留容量，避免反复扩容
out += "flowchart TD\n";     // 追加 C 字符串
out += std::to_string(42);   // int → 字符串
out.push_back('\"');         // 追加单个字符
std::string sub = s.substr(pos, len);
std::size_t p = s.find(x);   // 找不到返回 std::string::npos

// 格式化毫秒 → 秒（需要 <cstdio>）
char buf[32];
std::snprintf(buf, sizeof(buf), "%.1fs", static_cast<double>(ms) / 1000.0);
std::string text(buf);
```

## 本关三个函数（src/graph/mermaid.h）

```cpp
std::string escapeMermaidLabel(const std::string &s);
std::string emitMermaid(const Run &run);
std::string emitTimeline(const Run &run);
```

## 需要的数据字段（model.h / extract.h / 本关新增 enum）

```cpp
struct Run  { std::string id; std::vector<Step> steps; long long startMs, endMs; };
struct Step { int index; long long startMs, endMs; std::string model;
              std::vector<ToolExec> tools; std::string outputText;
              Usage usage; bool complete; };
struct ToolExec { std::string toolCallId, name, argsJson, resultJson;
                  long long startMs, durationMs; ErrorKind error;
                  bool durationInferred; };
// error: none / inferred / confirmed —— 分别对应无色 / 琥珀 / 红
```

## 判据

- **id 与标签分离**解决的是"改显示文字不用改边"——边的两端引用的是 id，不是显示文本
- **文本图 vs PNG**：可 diff、可 grep、可嵌 Markdown、零前端依赖；PNG 生成需要图形库且改动无法 review
- 失败分级上色：`confirmed` 红、`inferred` 琥珀、`none` 不着色——与 T5.4 的分级语义一脉相承
