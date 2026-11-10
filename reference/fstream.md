# `<fstream>` 文件流原型

## 写：`std::ofstream`

```cpp
// 构造
ofstream();
explicit ofstream(const std::string& path,
                  ios_base::openmode mode = ios_base::out);

// 打开 / 关闭 / 查询
void open(const std::string& path, ios_base::openmode mode = ios_base::out);
void close();
bool is_open() const;

// 写
ostream& write(const char* s, std::streamsize n);   // 原始字节，n 字节
ostream& put(char c);
ostream& operator<<(T value);                       // 格式化输出

// 冲刷
ostream& flush();

// 移动有，拷贝没有
ofstream(ofstream&&) noexcept;
ofstream& operator=(ofstream&&) noexcept;
ofstream(const ofstream&) = delete;
```

`std::streamsize` == `long long`（有符号）。传 `size_t` 会警告，要显式转换。

## 读：`std::ifstream`

```cpp
explicit ifstream(const std::string& path,
                  ios_base::openmode mode = ios_base::in);

bool is_open() const;
void close();

// 读
istream& read(char* s, std::streamsize n);          // 原始字节
istream& getline(char* s, std::streamsize n);       // C 风格，少用
```

## 整文件读进 string（惯用法）

```cpp
#include <fstream>
#include <string>
#include <iterator>     // istreambuf_iterator

std::ifstream in(path);                 // 默认 ios::in
if (!in.is_open()) { /* 失败处理 */ }

std::string text((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
// 括号不能少 —— 否则变成"函数声明"（most vexing parse）
```

替代写法（逐行）：

```cpp
std::string line, text;
while (std::getline(in, line)) {
    text += line;
    text += '\n';
}
```

## 打开模式

| 标志 | 含义 |
|---|---|
| `std::ios::in` | 可读（`ifstream` 默认） |
| `std::ios::out` | 可写（`ofstream` 默认） |
| `std::ios::app` | 每次写前移到末尾（追加） |
| `std::ios::trunc` | 打开时清空（`out` 的默认行为） |
| `std::ios::binary` | 不做 `\n` ↔ `\r\n` 转换 |

**注意**：`ofstream` 默认 `out | trunc`——不写 `app` 就等于每次打开清空文件。

## 状态位

```cpp
bool is_open() const;                    // 文件是否成功打开（结构性）
bool good() const;                       // 一切正常
bool fail() const;                       // 上次操作失败
bool bad() const;                        // 不可恢复错误
explicit operator bool() const;          // 等价 !fail()

// 清状态（恢复可用）
void clear();
```

- `open()` 失败**不抛异常**，只置 `failbit`
- 构造后查 `is_open()`；每次 `write` 后查 `good()` / `!fail()`

## 生命周期

`ofstream` / `ifstream` 析构时自动 flush + close（它们自己就是 RAII 类）。
