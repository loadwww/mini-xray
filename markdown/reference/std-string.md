# `std::string` 原型速查（`<string>`）

T3.1 路由匹配只用到这几样。列的是**签名 + 语义 + 坑**，不是教程。

## 长度 / 判空

```cpp
size_type size() const noexcept;         // 字符数（不是字节数；中文在 UTF-8 里算 3 个 char）
bool empty() const noexcept;             // size() == 0
void clear() noexcept;
```

## 取字符

```cpp
char&       operator[](size_type i);     // 不检查越界 —— i >= size() 是 UB，不是抛异常
const char& operator[](size_type i) const;
char&       at(size_type i);             // 检查越界 → 抛 out_of_range
char&       back();                      // 最后一个字符，空串调用是 UB
char&       front();                     // 同上
```

**规则**：先 `if (i < s.size())` 再用 `s[i]`。`s.size() - 1` 在空串上会下溢成巨大的数（`size()` 是 `size_t` 无符号），这个坑本项目已经踩过一次。

## 比较 / 判前缀（比 substr 省一次拷贝）

```cpp
int compare(const string& s) const;                          // <0 / 0 / >0
int compare(size_type pos, size_type n, const string& s) const;  // 从 pos 取 n 个字符跟 s 比
int compare(size_type pos, size_type n, const char* s) const;
```

判前缀的正确姿势：

```cpp
if (path.size() >= prefix.size() && path.compare(0, prefix.size(), prefix) == 0) { ... }
```

`compare(0, prefix.size(), prefix)` 返回 0 表示前 `prefix.size()` 个字符正好等于 `prefix`。
注意 `pos > size()` 会抛 `out_of_range`，所以 `path.size() >= prefix.size()` 必须先判。

**不要用 `starts_with`**：那是 C++20，本项目锁 C++17。（用也能编，MSVC 会提醒 `/std:c++20`。）

## 截取

```cpp
string substr(size_type pos = 0, size_type n = npos) const;
```

- `pos > size()` → **抛 `out_of_range`**（`pos == size()` 合法，返回空串）
- `n` 超出剩余长度会自动截到末尾，不报错

## 插入 / 拼接

```cpp
string& insert(size_type pos, const string& s);   // pos > size() → 抛 out_of_range
string& append(const string& s);
string& operator+=(const string& s);
// 非成员：
string operator+(const string& a, const string& b);   // 可能分配新内存
```

在开头插一个字符：`s.insert(0, 1, '/')` 或 `s.insert(s.begin(), '/')`。

## 查找

```cpp
size_type find(const string& needle, size_type pos = 0) const;   // 找不到返回 string::npos
size_type rfind(...) const;
static constexpr size_type npos = -1;    // 注意：是 size_type 类型的最大值，不是 int 的 -1
```

判"找不到"只能写 `== string::npos` 或 `== std::string::npos`，别写 `== -1`（类型提升能过，但会报警告，且 `size_type` 是 64 位时语义混乱）。

## 本项目相关的三个坑

1. **`size()` 是无符号**：`s.size() - 3` 在 `s.size() < 3` 时下溢成天文数字。T2.2 的 `scanned - 3` 就踩过，正确写法是 `scanned > 3 ? scanned - 3 : 0`。
2. **`operator[]` 不检查**：`path[prefix.size()]` 在 `path.size() == prefix.size()` 时越界。必须先判长度，或改用 `path.size() > prefix.size()` 分支。
3. **`compare` 的 n 参数**：`compare(0, n, s)` 里若 `n > size()`，标准规定实际比较的是 `min(n, size()-0)` 个字符——不会抛异常，但会让"长度不够也算相等"成立。所以长度判断还是要自己写。
