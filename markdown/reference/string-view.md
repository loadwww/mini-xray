# std::string_view（C++17）与字符分类

> 用在：T4.1 `ChunkedDecoder::feed(std::string_view)` / `parseSizeLine`

## 它是什么

```cpp
namespace std {
template<class CharT, class Traits = char_traits<CharT>>
class basic_string_view {   // 头文件 <string_view>
public:
    constexpr basic_string_view() noexcept;                  // 空
    constexpr basic_string_view(const CharT* s, size_type n); // 指针 + 长度
    basic_string_view(const basic_string<CharT,Traits,Alloc>&) noexcept;  // string → view，隐式
    // 从 const char* 构造：会 strlen，O(n)
};
using string_view = basic_string_view<char>;
}
```

**两个字段：`const char* data_` + `size_type size_`。它不拥有任何内存、不负责释放。**

一句话：`string_view` = "我只看，不碰，也不负责它活着"。所以它便宜（16 字节，拷贝 = 拷两个字段），也危险（生命周期靠调用方保证）。

## 成员函数（本关会用到的）

```cpp
constexpr const char* data()  const noexcept;   // 可能不带结尾 '\0'！
constexpr size_type   size()  const noexcept;
constexpr bool        empty() const noexcept;

constexpr void remove_prefix(size_type n);      // 前移起点，n > size() 是 UB
constexpr void remove_suffix(size_type n);      // 缩小长度
constexpr string_view substr(size_type pos = 0,
                             size_type n = npos) const;   // pos > size() 抛 out_of_range

constexpr size_type find(string_view v, size_type pos = 0) const noexcept;  // 没找到返回 npos
constexpr size_type find(char c, size_type pos = 0) const noexcept;
constexpr int       compare(string_view v) const noexcept;   // <0 / 0 / >0
constexpr bool      starts_with(string_view v) const noexcept;  // C++20
constexpr bool      ends_with(string_view v) const noexcept;    // C++20
```

C++17 没有 `starts_with` / `ends_with`（那是 C++20），所以判断前缀要用 `compare(0, n, prefix) == 0` 或者 `substr(0, n) == prefix`。

## 三个必须记住的坑

**1. 悬垂（最常见）**

```cpp
std::string_view bad() {
    std::string s = "hello";
    return s;          // 返回 view，s 在函数返回时已析构 → 悬垂
}
```

编译能过（有时连警告都没有），运行到读的时候是 UB。**view 的生命周期不能超过它指向的对象。**

**2. 不保证以 `'\0'` 结尾**

`sv.data()` 不能直接喂给 `printf("%s")`、`atoi`、`strlen`。要用就得先转 `std::string(sv)`，或者用带长度的 API（`fwrite(data, 1, size, f)`）。

**3. 和 `std::string` 混用时的隐式转换**

- `std::string` → `string_view`：**隐式**，零成本（就是取指针 + 长度）
- `string_view` → `std::string`：**显式**，要 `std::string(sv)` 或者 `sv` 构造时明确写出类型，因为这是**分配内存**的操作

所以函数参数用 `string_view` 可以让调用方传 `std::string` / 字符串字面量 / `const char*`，都不用拷贝。但**返回值绝对不要用 `string_view`**（除非你返回的是静态常量或调用方传进来的东西）。

## 什么时候用、什么时候别用

| 场景 | 选择 | 理由 |
|---|---|---|
| 只读参数的形参 | `string_view` | 调用方传什么进来都不用拷贝 |
| 要存起来 / 要返回 | `std::string` | view 不拥有，存下来就是悬垂 |
| 要改内容 | `std::string` | view 是 const 的（只读） |
| 要保证 `'\0'` 结尾 | `const std::string&` 或 `const char*` | view 不保证 |
| 跨线程传递 | `std::string` | 除非能证明数据活得够久 |

## 本关的具体用法

```cpp
std::string ChunkedDecoder::feed(std::string_view in);   // 只读，不存 → 对
```

`feed` 内部必须把 `in` **拷进 `buf_`**（`buf_.append(in.data(), in.size())`），因为调用方的缓冲区在 `feed` 返回后就可能被复用（socket 的 recv buffer 每次都会被覆盖）。**只读参数用 view，要留存就拷贝 —— 这两句话不矛盾。**

注意 `in.empty()` 时 `in.data()` 可能是 `nullptr`，`append(nullptr, 0)` 在标准里属于边界情况，先判空更稳：

```cpp
if (!in.empty()) buf_.append(in.data(), in.size());
```

## 字符分类 `<cctype>`

```cpp
int isdigit(int ch);    // '0'-'9'
int isxdigit(int ch);   // '0'-'9' 'a'-'f' 'A'-'F'   ← 本关用它
int isspace(int ch);
int isalpha(int ch);
int tolower(int ch);    // 'A'-'Z' → 'a'-'z'，其它原样返回
int toupper(int ch);
```

**坑**：这些函数接受 `int`，且值必须是 `unsigned char` 能表示的范围或 `EOF`。直接传 `char` 时，如果 `char` 是 signed 且字节 >= 0x80（UTF-8 汉字的一部分），会被当成负数 → **UB**。写法：

```cpp
std::isxdigit(static_cast<unsigned char>(c))
```

## 十六进制字符 → 数值

```cpp
char c = ...;
int d;
if (c >= '0' && c <= '9')      d = c - '0';
else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
else                           /* 非法 */;
```

先 `tolower(c)` 就能把后两个分支合成一个：

```cpp
const char lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
if (lc >= '0' && lc <= '9') d = lc - '0';
else if (lc >= 'a' && lc <= 'f') d = lc - 'a' + 10;
else /* 非法 */;
```

另一种写法是 `std::from_chars(buf, buf + n, value, 16)`（`<charconv>`），它顺便返回"解析到哪一位"和错误码，但它在 MSVC 上是 C++17 才完整支持，且不处理 `0x` 前缀 —— 本关手写更直观。
