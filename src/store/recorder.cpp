#include "store/recorder.h"

#include <exception>
#include <utility>

#include "store/redact.h"

namespace mxr {
namespace {

// 事件里的 body 要截断：events.jsonl 是"给人看、给工具分析"的，不是数据备份。
// 一个会话几十个请求，每个 body 都完整存下来的话，文件很快就几百 MB。
// 256KB 这个值够装下常见的 OpenAI 响应（含 usage），又不至于失控。
//
// 坑：按字节截断可能把一个 UTF-8 汉字切成两半，产生非法字节。
// JSON 用的是 dump()，非法字节会被替换成 \ufffd 之类的转义 —— 不崩，但那段
// 文字会花。真要严谨就按 UTF-8 的字符边界截，M7 再说。
std::string capBody(std::string s)
{
    constexpr std::size_t kMax = 256 * 1024;
    if (s.size() <= kMax) {
        return s;
    }
    s.resize(kMax);
    s += "...<truncated>";
    return s;
}

} // namespace

Recorder::Recorder(const std::string& path) : writer_(path)
{
    // writer_ 已经在初始化列表里打开了文件（打不开就抛，进程起不来 —— 这是对的：
    // 记录器坏了，整个 serve 就没意义了）。
    // 线程放到构造函数体里起，而不是初始化列表：要确保 writer_ 已经就位。
    th_ = std::thread([this] { run(); });
}

// ═══ T3.3 填空（五）：flush = 收工 + 排空 ═════════════════════════
//
// 要做三件事，顺序不能变：
//   1. 加锁把 stopping_ 置 true（**必须在锁里改**，因为写线程在锁里读它）
//   2. 通知写线程（notify_all），否则它还在 cv_.wait 上睡着，join 会永远等下去
//   3. if (th_.joinable()) th_.join();
//
// 为什么不能"直接 join 完事"：
//   写线程的等待条件是 `stopping_ || !q_.empty()`。只要队列非空它就不会睡，
//   所以 join 之前**不需要**手工清空队列 —— 它会自己把剩下的写完（drain），
//   发现队列空了才退出。这正是"进程退出时事件不丢"的关键。
//
// 为什么 notify_all 而不是 notify_one：
//   只有一个写线程，notify_one 也够。但 notify_all 语义上更稳 —— 以后万一
//   多出第二个消费者（比如同时推 UI 的实时流），这里不用改。
//
// 为什么可以重复调用：
//   join 过之后 th_.joinable() 就是 false，第二次进来只做一次无意义的赋值。
//   析构和显式 flush 都调它，不会出事。
void Recorder::flush()
{
    /*⑤*/
    {
        std::unique_lock<std::mutex>lk(mu_);
        stopping_=true;
    }
    cv_.notify_all();
    if(th_.joinable())th_.join();

}

Recorder::~Recorder()
{
    flush();
}

// ═══ T3.3 填空（三）：push = 入队 ════════════════════════════════
//
// push 是**热路径**上唯一被调用的成员（每个请求两次），所以它必须：
//   不加锁做任何 I/O、不分配大对象、不抛异常、永远立刻返回。
//
// 要做的事（全部在锁内）：
//   1. 如果 stopping_（正在析构）→ 直接 return，别再往队列里塞
//   2. 如果 q_.size() >= kMaxQueued → ++dropped_ 然后 return（丢新的，见下面说明）
//   3. ev.seq = ++seq_          ← seq 必须在这里分配，理由见下
//   4. q_.push(std::move(ev))   ← move，不要拷贝（body 可能几百 KB）
// 锁外：
//   5. cv_.notify_one()         ← 唤醒写线程
//
// 为什么 seq 在入队时分配，而不是在写线程里分配：
//   写线程里分配 = 行号（1,2,3...），没有任何信息量。
//   入队时分配 = "事件被观测到的先后顺序"，多线程下它仍然只是近似，
//   但至少同一次请求的 req(seq=k) / resp(seq=m) 的相对次序是对的，
//   而且它是**连续无空洞**的 —— M5 检查"有没有丢事件"就靠这个。
//
// 为什么队列满时丢"新的"而不是丢"旧的"：
//   旧的已经在队列里排了很久，马上就会被写出去（队列是 FIFO）；
//   而丢掉新的，代价是"最近发生的事没被记"，但从时间上看正好是"磁盘跟不上
//   的那一小段"。不管丢哪头，都得计数（dropped_），否则丢了你也不知道。
void Recorder::push(Event ev)
{
    {
        std::unique_lock<std::mutex> lk(mu_);
        if(stopping_){
            return;
        }
        if(q_.size()>=kMaxQueued){
            ++dropped_;
            return;
        }
        ev.seq = ++seq_;
        q_.push(std::move(ev));
        lk.unlock();
    }
    cv_.notify_one();
}

// ═══ T3.3 填空（四）：写线程主循环 ═══════════════════════════════
//
// 这是一个标准的"生产者-消费者"循环，骨架只有四步：
//
//   循环 {
//     a) 加锁，在条件变量上等：等到"有活了"或者"该收工了"
//     b) 取出一个元素（如果队列空 → 说明是收工信号，return 退出循环）
//     c) 【解锁】脱敏 + 截断 + 序列化 + 写盘（I/O 必须锁外做）
//     d) 计数
//   }
//
// 关键细节逐个说：
//
// 1) 为什么用 unique_lock 而不是 lock_guard：
//    cv_.wait 需要"等的时候把锁放开、醒了再抢回来"，这个能力只有 unique_lock 有。
//
// 2) wait 的谓词为什么写 `stopping_ || !q_.empty()`：
//    这是防"虚假唤醒"（spurious wakeup）—— 条件变量可能在没人 notify 的情况下
//    自己醒过来。带谓词的 wait(lk, pred) 会在醒来后重新检查 pred，不满足就继续睡。
//    不写谓词的裸 wait 是最经典的 bug 来源。
//
// 3) 为什么 `if (q_.empty()) return;` 放在 wait 之后：
//    能走到这里，说明谓词成立了。谓词成立只有两种可能：
//      - 队列非空      → 有活干，继续
//      - 队列空        → 那只可能是 stopping_ 为真（收工信号）
//    所以"被叫醒且队列空"就等于"该退出了"。这条判断同时实现了 **drain**：
//    写线程会一直取到队列空才退，不会丢下没写完的事件。
//
// 4) 为什么写盘必须解锁：
//    写盘可能卡几百毫秒。锁在手里的话，这期间所有 push 都被挡住 ——
//    异步就白做了，等于把磁盘的慢传染回转发线程。锁只罩"取一个元素"这一瞬间。
//
// 5) 为什么 try/catch 包住 append：
//    异常从线程函数里逃出去 = std::terminate，整个进程直接死（还不能被捕获）。
//    写盘失败（磁盘满、文件被删）不该杀死代理，所以吞掉 + 计数。
//
// 6) 脱敏为什么放在这里，而不是 handler 里：
//    脱敏要跑正则，body 大时是毫秒级的开销。它属于"落盘前"的工作，
//    和写盘一起放在写线程，转发路径就一点都不沾。
//
// 用得到：
//   std::unique_lock<std::mutex> lk(mu_);
//   cv_.wait(lk, [this]{ return stopping_ || !q_.empty(); });
//   ev = std::move(q_.front());  q_.pop();
//   writer_.append(toJson(ev));
//   ++written_ / ++dropped_（atomic 直接 ++ 就行）
//   别忘了 #include "store/redact.h"（已经 include 了）
void Recorder::run()
{
    Event ev;
    while(1){
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk,
                [&]{return stopping_||!q_.empty();}
        );
        if(q_.empty())return;
        ev=std::move(q_.front());  
        q_.pop();
        lk.unlock();

        ev.headers=redactHeaders(ev.headers);
        ev.body=capBody(redactBody(ev.body));
        try{
            writer_.append(toJson(ev));
            ++written_;

        }catch(...){
            ++dropped_;
        }
    }
}

} // namespace mxr
