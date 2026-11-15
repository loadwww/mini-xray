# 建链 / 滚动前缀哈希 / 稳定 id 速查（T5.3）

> 本关把扁平事件还原成执行链。核心只有三个动作：配对、算前缀、判父子。

## 一、滚动前缀哈希

```cpp
// 单条消息 → 固定短串；分隔符用 \x1f 避免 "ab"+"c" 与 "a"+"bc" 撞车
std::string messageHash(const Message &m);
// 链式单步：把前一步的哈希也喂进来
std::string chainHash(const std::string &prev, const Message &m);

std::vector<std::string> hashes;
std::string prev = "固定种子";
for (const auto &m : messages) {
    prev = chainHash(prev, m);
    hashes.push_back(prev);       // hashes[i] = 前 i+1 条的指纹
}
```

**为什么必须链式**：只把每条消息各自哈希再拼起来，`[A,B,C]` 和 `[C,B,A]`
会得到同一个集合，丢掉顺序信息；把 `prev` 喂进下一步才有"前缀"语义。

**判前缀的公式**：

```
A 是 B 的严格前缀  ⟺  A 非空 且 |A| < |B|
                    且  prefixHashes(B)[|A|-1] == prefixHashes(A).back()
```

**为什么不用 `std::hash`**：标准只保证同一进程内一致，结果允许随实现/版本变化；
写进文件、下次重放再读时可能对不上。用固定算法的哈希（如 FNV-1a）才有稳定 id。

```cpp
std::uint64_t h = 14695981039346656037ull;       // FNV offset basis
for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }  // FNV prime
char buf[17]; std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
```

## 二、稳定 id

```
id = "run-" + 根事件 ts_ms + "-" + 根指纹前 8 位 + "-" + 该 run 的序号
```

- **时间取自文件里的事件**，不是 `nowMs()` —— 验收要求"重复执行同一份
  events.jsonl，run id 保持稳定"；当前时间每次都不同
- 根指纹 = 根 hop 的 `prefixHashes(messages).back()`
- 序号区分同一毫秒内出现的多个根
- 代价：碰撞（不同序列撞同一哈希）理论上存在，所以保留全量比较版对账

## 三、配对与排序原型

```cpp
#include <algorithm>
#include <fstream>
#include <map>

std::ifstream in(path);
while (std::getline(in, line)) { /* 一行一个 json */ }

std::map<std::string, Hop> byHop;          // 按 hop 分组，键自动有序
std::sort(v.begin(), v.end(),
          [](const Hop &a, const Hop &b) { return a.seq < b.seq; });

// 一行安全解析（坏行跳过）
auto j = nlohmann::json::parse(line, nullptr, false);
if (j.is_discarded()) continue;
j.value("type", "") / j.value("hop", "") / j.value("seq", 0LL)
 / j.value("ts_ms", 0LL) / j.value("body", std::string());
```

`value<T>` 的默认值决定推导类型：`long long` 写 `0LL`，字符串写 `std::string()`。

## 四、判父子的两条硬规则

| 规则 | 为什么 |
|---|---|
| A 必须是 B 的**严格**前缀（更短） | 同一次请求重发不算新一步 |
| 共同前缀**不能只有 system 消息** | 两个独立任务通常共享同一 system prompt；只凭它就把两条任务串成一条链是错的 |

实现上："A 只有 system" = A 开头连续 `role=="system"` 的条数 ≥ A 的总条数。

## 五、常见错误的症状对照

| 写错的地方 | 症状 |
|---|---|
| 哈希不 chain，只拼单条哈希 | 顺序丢失，`[A,B]` 与 `[B,A]` 判成同一前缀 |
| 用 `std::hash` 当持久化指纹 | 换编译器 / 换进程后 run id 变了 |
| run id 用 `nowMs()` | 每次重放 id 都不同，验收直接挂 |
| 漏掉 system-only 排除 | 共享 system prompt 的两个独立任务被串成一个 run |
| 找父步时从最旧的开始 | 多轮链会接到错误的祖先上（要找**最近**的） |
| 用 `>=` 而不是 `<` 判长度 | 相同 messages 被当成父子，步数虚增 |
| 流式 hop 只看 `resp.body` | 流式 resp.body 是空的，结论在 `sse_end` 里，`outputText` 全空 |
| 配对时丢掉没有 req 的 resp | 孤儿事件悄悄消失，seq 连续性无法检查 |
