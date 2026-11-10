# `std::optional<T>` 原型（C++17，`<optional>`）

## 构造

```cpp
std::optional<int> a;                    // 空
std::optional<int> b = 42;               // 有值（隐式）
std::optional<int> c{std::in_place, 42}; // 原地构造（复杂类型用）
std::optional<Config> d = std::nullopt;  // 显式空
```

## 查询 / 取值

```cpp
bool has_value() const;                  // 或直接 if (opt)
explicit operator bool() const;          // 同上

T&       value();                        // 空 → 抛 bad_optional_access
const T& value() const;
T        value_or(U&& fallback) const;   // 空 → 返回 fallback

T&       operator*();                    // 空时 UB（不检查，快）
const T& operator*() const;
T*       operator->();                   // 等价 (*opt).member
const T* operator->() const;
```

## 修改

```cpp
void reset();                            // 变空（会析构内部对象）
T&   emplace(Args&&... args);            // 就地重建
void swap(optional& other);
```

## 三个要点

1. **它和指针的区别**：`optional<T>` 把对象**存在自己内部**（可能有一点字节浪费），不堆分配、不担心悬垂；指针/裸标志位表达不了"有值但值是 0"这类语义
2. **它和异常的区别**：失败是**预期内的常规分支**（配置文件不存在、查找未命中）用 optional；失败是**违反约定、调用方必须知道**才用异常
3. **`*opt` 不检查**——和裸指针一样危险。要么先 `if (opt)`，要么 `value_or`，要么 `value()`（抛异常）
