# 超步归并原型速查（T5.4）

这里只放函数原型。写代码时卡在"这个函数怎么调"就翻这里。

## 本关要自己组织的三个函数（链内互相调用）

```cpp
// 已铺好的依赖（chain/link.h）：
std::vector<std::string> prefixHashes(const std::vector<Message> &messages);
// back() 即整条链指纹 —— Step.requestFingerprint 存的就是它

// 本关填空的三个（chain/superstep.h）：
ErrorKind classifyToolError(const std::string &resultJson, bool protocolIsError);
std::vector<ToolExec> toolsFromDelta(const std::vector<ToolCall> &prevCalls,
                                     const std::vector<Message> &curMsgs,
                                     long long windowStartMs, long long windowMs);
void mergeSupersteps(std::vector<Run> &runs, const std::vector<Hop> &hops);
```

## 涉及的结构体字段（写的时候别凭记忆）

```cpp
// parse/extract.h —— 意图与消息
struct ToolCall {           // 模型发起的调用
    std::string id;         // 对应消息里的 "tool_call_id"（snake_case！）
    std::string name;
    std::string argsJson;   // 原始参数文本
};
struct Message {            // 请求 messages[] 里的一条
    std::string role;       // "tool" 的才是结果
    std::string content;    // 结果文本就在这
    std::string toolCallId;
    std::string name;
};

// model/model.h —— 执行记录（本关的产出物）
struct ToolExec {
    std::string toolCallId;
    std::string name;
    std::string argsJson;
    std::string resultJson;
    long long startMs = 0;
    long long durationMs = 0;
    ErrorKind error = ErrorKind::none;
    bool durationInferred = false;
};
enum class ErrorKind { none, inferred, confirmed };
```

## nlohmann：不抛的解析 + 只读判断（T5.2 同款套路）

```cpp
// 不抛异常的解析：坏了得到 discarded，不是异常
nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
bool bad = j.is_discarded();          // 坏 JSON
bool obj = j.is_object();             // 是不是 {}
bool hasErr = j.contains("error");    // 只读判断，不会像 operator[] 那样插 null
bool isErr = j.value("is_error", false); // 缺键/类型不符/null 都安全回落默认值
// j["error"] 危险：const json 上没有 operator[]，能用的地方会**插入**默认值
```

## std::map：指纹 → 下标 的反查表（T5.3 同款套路）

```cpp
std::map<std::string, int> byFp;
byFp[fp] = 42;                        // 写：key 已存在则覆盖 value
auto it = byFp.find(s.requestFingerprint);
if (it != byFp.end())                 // 找到：it->first 是 key，it->second 是 value
    ...;
// operator[] 找不到时会**默认构造一个 value 塞进去**——只读场景必须用 find
```

## std::max：两个 long long 的钳制

```cpp
#include <algorithm>
long long w = std::max(0LL, next->startMs - chain[i]->endMs);
// 两边都写成 long long（0LL），混进 int 会有重载歧义/窄化
// 场景：乱序流量里下一请求可能比上一响应还早 → 区间为负，钳成 0
```

## 本关的判定顺序（口试高频）

1. `protocolIsError == true` → **confirmed**（协议明说，可信）
2. 结果 parse 成 JSON 对象且含 `"error"` 键 → **confirmed**
3. 小写化后命中词表（error/failed/exception/not found 任一子串）→ **inferred**（可能误报）
4. 都不中 → **none**

为什么 confirmed 排在 inferred 前面：可信度高的证据先判，先到先得；
文本启发式放最后，因为它只是"像"，工具完全可能把 "error" 当正常返回值。

为什么耗时全标 `durationInferred`：代理只能看到"上一响应结束 → 下一请求开始"
这一个区间，N 个并行工具共享它，拆不出单个的精确耗时。M6 的 UI 要给推断值打 "~"。
