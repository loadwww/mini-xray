# nlohmann/json 原型（v3.11.3，third_party/nlohmann/json.hpp）

```cpp
#include <nlohmann/json.hpp>
using json = nlohmann::json;        // 惯例别名
```

## 解析 / 序列化

```cpp
static json json::parse(const std::string& text);    // 失败抛 parse_error
std::string json::dump(int indent = -1) const;       // indent>=0 时缩进美化
```

## 访问

```cpp
bool contains(const std::string& key) const;         // 字段存在？（不抛异常）
bool is_object() const;
bool is_array() const;
bool is_string() const;
bool is_number_integer() const;

json&       at(const std::string& key);              // 缺字段抛 out_of_range
const json& at(const std::string& key) const;

T json::get<T>() const;                              // 类型不符抛 type_error
template<class T>
T value(const std::string& key, const T& default) const;   // 缺失→默认；类型错→抛 type_error

std::size_t size() const;                            // 数组长度 / 对象字段数
json& operator[](const std::string& key);            // 不检查，缺就直接创建空值（危险）
```

遍历数组：

```cpp
for (const json& el : arr) { ... }                   // 元素是对象/标量
```

## 构造（把内存里的结构体写回 JSON）

```cpp
json j;                                              // 默认构造 = null，赋值后自动变类型
j["port"] = 8080;                                    // 用 operator[] 当写入点（键不存在则新建）
j["proxy"]["injectUsage"] = true;                    // 一层层往下建，中间层自动变成 object
j["proxy"]["upstreams"].push_back({{"match", "/x"}, {"url", "http://u"}});  // 元素是 object
j["tags"] = {"a", "b"};                              // 列表初始化 → 数组

std::string s = j.dump(2);                           // 2 空格缩进；dump() 无参 = 紧凑单行
bool ok = json::accept(text);                        // 只校验语法、不建对象（可选）
```

要点：**写入**用 `operator[]`（会创建），**读取**用 `value()/at()`（不会偷偷造值）。
`push_back` 作用在 null 上时 nlohmann 会自动把它变成数组——方便，但也意味着键名拼错不会报错，只会静默多出一层。

## 异常（均继承 `nlohmann::json::exception`）

```cpp
catch (nlohmann::json::parse_error& e)   // 语法错，e.what() 含 "byte position N"
catch (nlohmann::json::type_error& e)    // 类型错（如 .302）
catch (nlohmann::json::out_of_range& e)  // at() 找不到键
catch (nlohmann::json::exception& e)     // 一把抓
// e.what() → const char*，可直接 << 到 cerr
```

## 三点使用纪律

1. `value()` 的默认参数决定返回类型：`value("root", std::string{})`，写 `""` 推导成 `const char*` 会编译失败
2. `at()` / `contains()` 用于**结构性必有**的字段；`value()` 用于**可降级**的字段
3. `operator[]` 遇到不存在的键会**插入 null**，读配置时一律不用它（这是"为什么 value 比 [] 安全"的一半答案）
