# nlohmann ADL 序列化原型速查（T5.1）

> 把自定义结构变成 `json` 并从 `json` 读回来。这一关的验收是
> `from_json(to_json(x)) == x`，所以下面每个函数的返回值语义都要看清。

## 一、ADL 的两个自由函数

```cpp
#include <nlohmann/json.hpp>

namespace myns {
struct Thing { int a = 0; };

// 必须与类型在**同一个 namespace**，nlohmann 靠参数依赖查找（ADL）找到它
void to_json(nlohmann::json &j, const Thing &v);
void from_json(const nlohmann::json &j, Thing &v);
} // namespace myns
```

```cpp
myns::Thing t{7};
nlohmann::json j = t;              // 自动调 to_json(j, t)
myns::Thing back = j.get<myns::Thing>();  // 自动调 from_json(j, back)
```

- 两个函数都返回 `void`：`to_json` 写进 `j`，`from_json` 写进 `v`
- 声明必须在**使用点之前可见**（`json j = t;` 的那一行之前）
- 嵌套结构：`j["child"] = v.child;` 会自动递归找 `child` 类型的 `to_json`；
  `j.value("child", Child{})` 会自动递归找 `from_json`
- `std::vector<T>` 同理，nlohmann 自带 vector 支持，逐元素递归

## 二、写：`to_json`

```cpp
void to_json(nlohmann::json &j, const Thing &v) {
    j["a"] = v.a;                        // 写入点自动建键
    j["nested"] = v.nested;              // 嵌套直接赋值
    j["items"] = v.items;                // vector<T> 直接赋值
}

// 也可以一次性构造
j = {{"a", v.a}, {"nested", {{"x", 1}}}};
```

枚举不能自动序列化：自己映射成**字符串**（数字会在插入新值时让历史文件错位）。

## 三、读：三个取值方式的差别

```cpp
j.value("key", 默认值);   // 缺键 → 返回默认值；不抛异常（离线读文件首选）
j.contains("key");        // 只判存在
j.at("key");              // 缺键 → 抛 out_of_range
j["key"].get<T>();        // 非 const json 的下标遇缺键会**插入 null**，再 get 可能抛
```

| 写法 | 缺键 | 类型不符 | 适用 |
|---|---|---|---|
| `value(k, d)` | 返回 d | 可能抛 type_error | 读历史文件、字段可缺省 |
| `at(k)` | 抛 | 可能抛 | 要求字段必须存在 |
| `get<T>()` | 抛 | 抛 | 已经确认过形状 |

**默认值类型要对齐**：`long long` 字段写 `0LL`，`int` 写 `0`，`bool` 写 `false`。
`value<T>` 的 T 由默认值推导，写错类型会抛。

## 四、枚举与字符串互转

```cpp
const char *toString(E k) {
    switch (k) { case E::a: return "a"; case E::b: return "b"; }
    return "a";
}
E fromString(const std::string &s) {
    if (s == "b") return E::b;
    return E::a;              // 未知值给一个安全默认，别让老文件搞崩解析
}

// 用的时候
j["error"] = toString(v.error);
v.error = fromString(j.value("error", std::string("none")));
```

## 五、相等比较（C++17）

```cpp
bool operator==(const Thing &a, const Thing &b) {
    return a.a == b.a && a.nested == b.nested && a.items == b.items;
}
```

- C++17 **没有**默认 `operator==`（C++20 才有 `= default`），每个字段都要自己比
- `std::vector<T>` 自带 `operator==`，逐元素调用 `T::operator==`
- 用途：往返测试的判据。漏比一个字段 → 那个字段写错了测试也全绿，验证器失效
- 防御办法：测试里把每个字段逐个变异，断言结果必须判为不等

## 六、常见错误的症状对照

| 写错的地方 | 症状 |
|---|---|
| `to_json` / `from_json` 放错 namespace | 编译报 "no suitable conversion"，ADL 找不到 |
| 只声明没定义 | 链接错误 unresolved external |
| `from_json` 用 `j["k"].get<T>()` | 老文件缺键 → 插入 null → type_error，解析崩 |
| 默认值写成 `0` 但字段是 `long long` | `value` 推成 int，可能抛 type_error |
| 枚举存成整数 | 中间插入新枚举值后，所有历史文件语义错位 |
| 嵌套手写循环而不是直接赋值 | 代码变长，还容易漏掉空 vector 的情况 |
| `operator==` 漏字段 | 往返测试全绿但数据其实是错的 |
| 模型层 `#include <httplib.h>` | 离线 replay 和单测都得拖进整个 HTTP 库，解耦失效 |
