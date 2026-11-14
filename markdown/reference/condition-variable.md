# 线程同步与异步队列速查

T3.3 的 `Recorder` 用到：`std::thread` / `std::mutex` / `std::lock_guard` / `std::unique_lock` /
`std::condition_variable` / `std::queue` / `std::atomic` / `<filesystem>`。
`std::mutex` / `lock_guard` / `thread` 的基础在 [mutex-thread.md](mutex-thread.md)，这里补**条件变量那部分**。

## std::thread

```cpp
#include <thread>

std::thread t;                                  // 不关联任何线程
std::thread t(f, args...);                      // 立刻开始跑 f(args...)，参数按值拷贝（要引用用 std::ref）
std::thread t([]{ /*...*/ });                   // lambda 也行

bool ok = t.joinable();                         // 有没有关联一个还没被 join/detach 的线程
t.join();                                       // 等它跑完（阻塞当前线程）
t.detach();                                     // 撒手不管（本项目不用）
std::thread::id t.get_id();
void t.swap(other);                             // 交换
```

**最重要的一条规则**：`std::thread` 析构时如果还 `joinable()`，标准要求直接 `std::terminate()`。
所以每个 thread 都必须在某个确定的地方 `join()`（或 `detach()`，但那是另一种设计）。
本项目：构造时起线程，`flush()` / 析构里 join —— 这样"线程活多久"紧跟"对象活多久"。

其他容易忘的：

- 线程函数里**抛出的异常不能穿出线程**，逃出去一样是 `std::terminate`。客服端要么 try/catch 包住主体，要么保证不抛。
- 传参默认是**拷贝**；要改外面的变量得用 `std::ref(x)` 或者捕获指针/this。
- `join()` 只能调一次，第二次会抛 `std::system_error`。所以先判 `joinable()`。

## std::mutex / lock_guard / unique_lock

```cpp
std::mutex mu;

{ std::lock_guard<std::mutex> lk(mu); /* 临界区 */ }   // 构造加锁、析构解锁，不能手动解
{ std::unique_lock<std::mutex> lk(mu);  /* 临界区 */ } // 同上，但可以提前 unlock()、可以交给 cv 用
```

`unique_lock` 比 `lock_guard` 多出来的能力（都是条件变量需要的）：

```cpp
lk.unlock();                     // 手动放锁
lk.lock();                       // 再拿回来
bool held = lk.owns_lock();
```

## std::condition_variable

```cpp
#include <condition_variable>

std::condition_variable cv;

// 等待方（消费者）
std::unique_lock<std::mutex> lk(mu);
cv.wait(lk, []{ return /* 条件 */; });      // 谓词版：醒来后重新检查，不满足继续睡
cv.wait(lk);                                // 裸版：会虚假唤醒，几乎总是错的
cv.wait_for(lk, std::chrono::milliseconds(100), pred);   // 带超时，返回 bool
cv.wait_until(lk, timePoint, pred);

// 通知方（生产者）
cv.notify_one();     // 叫醒一个等待者
cv.notify_all();     // 叫醒全部
```

**语义要点**

1. `wait(lk, pred)` 等价于 `while (!pred()) wait(lk);`，并且**在睡眠期间把 lk 放开**，醒来后重新加锁。
   所以传进去的 `lk` 必须是锁着同一个 `mu` 的 `unique_lock`。
2. **谓词必须读"和 notify 同一个 mutex 保护的状态"**，否则会出现"检查完条件、还没睡下去、对方已经 notify"的丢信号。
   标准写法：状态变量（如 `q_.empty()` / `stopping_`）全在锁内读写，notify 可以在锁外。
3. `notify_one/all` 在锁内调也能工作，但会让被叫醒的线程立刻又卡在锁上 —— 放锁外更顺。
4. 虚假唤醒（spurious wakeup）是允许的：系统可能在没人 notify 时让你醒。所以永远带谓词。

**经典模板**

```cpp
// 消费者
for (;;) {
    T item;
    {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait(lk, [&]{ return stop || !q.empty(); });
        if (q.empty()) return;          // stop 且队列空 → 收工
        item = std::move(q.front());
        q.pop();
    }
    doWork(item);                        // 锁外做慢活
}

// 生产者
{
    std::lock_guard<std::mutex> lk(mu);
    if (q.size() >= kMax) { ++dropped; return; }   // 背压：满了就丢
    q.push(std::move(item));
}
cv.notify_one();
```

## std::queue

```cpp
#include <queue>

q.push(v);  q.emplace(args...);      // 入队（尾部）
q.front();                            // 看队头（不删）；空队列上调用是 UB
q.pop();                              // 删队头（**不返回元素**，所以要先 front() 再 pop()）
q.empty();  q.size();
```

`pop()` 只删不返回，是"先 `auto v = std::move(q.front()); q.pop();`"两步写法的原因。
（`std::queue` 没有迭代器，也没有 `clear()`，要清空只能循环 pop。）

## std::atomic

```cpp
#include <atomic>

std::atomic<long long> n{0};
++n;  n++;  n += 3;          // 原子读改写
long long v = n.load();      // 原子读
n.store(5);                  // 原子写
long long old = n.fetch_add(1);   // 返回旧值
```

什么时候用 atomic、什么时候用 mutex：

- **单个变量的计数/标志**（`dropped_`、`written_`、`stopping` 这种"只读一个值就能判断"）→ atomic 更轻。
- **一组数据要保持一致**（队列 + 标志 + 序号）→ 必须 mutex。`Recorder` 里 `q_` / `seq_` / `stopping_` 是一组的，统一由 `mu_` 保护，没有单独用 atomic。

`std::atomic` 的 `operator=` 是"赋值"不是"拷贝构造"，所以 `std::atomic` 不能拷贝 —— 含 atomic 成员的类默认就不可拷贝。

## <filesystem>（C++17）

```cpp
#include <filesystem>
namespace fs = std::filesystem;

fs::path p = "runs/20261114-153012";
bool made = fs::create_directories(p);          // 逐级创建，已存在返回 false（不报错）
bool made2 = fs::create_directories(p, ec);     // 不抛版本：错误写进 std::error_code
bool gone = fs::remove("a.jsonl");              // 删文件（不存在返回 false）
fs::remove_all(dir);                            // 递归删（慎用）
bool ex = fs::exists(p);
auto sz = fs::file_size(p);
fs::path parent = p.parent_path();
fs::path name   = p.filename();
```

两个建议：

- 需要"失败时自己处理"就用带 `std::error_code&` 的重载（我们的 `serve()` 就是这么做的，好给用户一句人话）。
- 路径拼接用 `fs::path` 的 `operator/`，别手工拼 `"/"` —— Windows 下混用 `/` 和 `\` 迟早出事。本项目为了简单用了字符串拼接（配置里全是正斜杠），属于有意的简化。
