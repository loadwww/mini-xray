# 并发原语原型（C++17 标准库：`<mutex>` / `<thread>`）

## std::mutex

```cpp
#include <mutex>

std::mutex m;                    // 不可拷贝、不可移动
void  m.lock();                  // 阻塞直到拿到锁；已持有再 lock = 未定义行为（通常死锁）
void  m.unlock();                // 没持有时 unlock = 未定义行为
bool  m.try_lock();              // 试着拿，拿到返回 true，拿不到立刻返回 false（不阻塞）
```

裸 `lock()` / `unlock()` 几乎不要手写——中间 throw 或提前 return 就永远不解锁。用下面的 RAII 包装。

## std::lock_guard（RAII，最简单）

```cpp
#include <mutex>

{
    std::lock_guard<std::mutex> lk(m);   // 构造时 lock()
    // ... 临界区
}                                        // 析构时 unlock()，异常也会解锁
```

- 没有 `unlock()` 成员，**不能提前放锁**：作用域结束才释放
- `std::lock_guard<std::mutex> lk(m, std::adopt_lock);` — 表示锁已经拿过了，构造时不再 lock
- 一般写成 `const auto& lk = ...` 不必，`lk` 是"锁的所有权凭证"，不读它的值

## std::unique_lock（需要提前放锁 / 条件变量时）

```cpp
#include <mutex>

std::unique_lock<std::mutex> lk(m);      // 构造即 lock（可移动，可手动控制）
lk.unlock();                             // 手动放锁
lk.lock();                               // 再拿回来
bool has = lk.owns_lock();               // 当前是否持有
```

只有需要"中途放锁"或配合 `condition_variable` 时才用它；否则 `lock_guard` 更省。

## std::thread

```cpp
#include <thread>

std::thread t(fn);                        // 新线程执行 fn（函数指针 / lambda / 可调用对象）
std::thread t([&]{ work(); });            // 捕获引用 → 注意生命周期，线程比对象活得久就是悬垂
bool alive = t.joinable();                // 是否还关联一个线程
void t.join();                            // 等它跑完（每个 thread 必须 join 或 detach，否则析构时 terminate）
void t.detach();                          // 放走，不等（本项目不用）
unsigned n = std::thread::hardware_concurrency();   // 建议线程数，可能是 0
std::this_thread::get_id();               // <thread>，当前线程 id
```

纪律：

1. **join 必须走到**。`std::thread` 析构时若仍 `joinable()` → `std::terminate()`。要么 RAII 包装（jthread/自定义），要么保证所有路径都 join。
2. 传给线程的 lambda 用 `[&]` 捕获时，被捕获的对象必须活到最后一次 join 之后。
3. `join()` 只能调一次；join 后再 `joinable()` 为 false。

## 和"线程安全"相关的几条硬事实

- **`std::ofstream` 的 `<<` 不是原子操作**：底层是多次 `write()`，两个线程同时写会交错。想不交错，只能自己上锁。
- **`ofstream` 对象本身不是线程安全的**：连"谁先谁后"都不保证，更别说字节不混。
- **`nlohmann::json` 对象不是线程安全的**：多个线程同时读写同一个 `json` 对象是 UB。安全做法是每个线程用自己的 `json`，序列化成 `std::string` 之后，只把"写字符串"这一步放进临界区。
- 锁保护的是**数据**，不是代码：所有访问同一份数据的路径，必须拿同一把锁。
