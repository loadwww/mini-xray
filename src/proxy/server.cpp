#include "proxy/server.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "common/clock.h"
#include "proxy/route.h"
#include "proxy/target.h"
#include "store/event.h"
#include "store/recorder.h"

// ═══ T3.2：非流式转发 ═════════════════════════════════════════════
//
// 这一关是整个项目第一次"真的把请求送出去"。链路：
//
//   curl → [httplib::Server 解析] → 【本文件 handler】
//            ① resolve：这条路径归哪条上游、剥完是什么路径
//            ② buildTarget：连哪台机器、请求行写什么
//            ③ httplib::Client 发出去、等响应
//            ④ 把响应搬回 res（状态码 + Content-Type + body）
//   mock ←──────────────────────────┘
//
// 三个必须想清楚的问题：
//
// 1) 为什么用 set_pre_routing_handler，不用 svr.Post("/v1/...", h)？
//    代理对 method 是**无感**的：GET 查模型、POST 发消息、DELETE 删文件，
//    走的都是同一条转发逻辑。按 method 各注册一个 handler，等于把 method 白名单
//    写死在代码里 —— 上游哪天支持了 PATCH，你还得改代理。
//    pre_routing 在"路由匹配之前"触发，返回 Handled 就终止后续匹配，
//    正好是"我不按路径分派，我自己决定怎么处理"。
//
// 2) 为什么不能 `res.body = r->body;` 就完事？
//    那样只搬了正文：状态码还是默认的 200（上游 400 也会变成 200），
//    Content-Type 丢了（客户端不知道这是 JSON 还是纯文本），
//    上游的 500/429 全被抹平成"成功"。抓包工具最忌讳这个 ——
//    它会让下游 SDK 的重试逻辑完全失效。
//
// 3) 哪些 header 能透传、哪些必须改？
//    必须改：Host（要变成上游的，httplib::Client 自己会填）、
//            Content-Length（body 可能变了，httplib 会按实际长度重算）
//    不能传：Connection / Transfer-Encoding / Keep-Alive 这类 **hop-by-hop**
//            header，它们只管"这一跳"，透传过去会让上游按错误的连接方式处理
//    要透传：Content-Type（上游靠它判断 body 格式）、Authorization（鉴权）、
//            Accept（客户端要什么格式）
//
// 4) 计时用哪个钟？
//    测"这次转发花了多久"要用 steady_clock（单调、不受系统改时间影响）；
//    要落盘给人看的"事件发生在几点"才用 system_clock。这一关只测耗时。

namespace
{

// ── Ctrl+C 的优雅退出 ────────────────────────────────────────────────
//
// 默认行为：Ctrl+C 直接 TerminateProcess —— 进程立刻消失，栈上所有对象的析构
// **都不会执行**。对我们的含义是：Recorder 的写线程被硬杀，队列里没写完的事件
// 丢了，文件还可能停在一行中间。
//
// 所以我们接管它：控制台事件来了就把 server 停掉。listen() 随即返回、serve()
// 正常 return、Recorder 析构把队列写空 —— 全程都是普通的栈展开。
//
// 两个细节：
//   1. 这个处理器跑在**操作系统另起的线程**上，不是我们的任何线程。所以里面
//      只能碰"别的线程已经准备好被从外部改"的东西：svr.stop() 和一个 atomic。
//   2. 返回 TRUE = "我处理了，别杀进程"。返回 FALSE 会继续走默认逻辑（直接终止），
//      等于没接管。
//   为什么放在匿名 namespace 而不是 serve() 里：控制台处理器是个 C 回调，
//   没有"闭包"这回事，只能靠全局变量找到那个 server。
httplib::Server *g_svr = nullptr;
std::atomic<bool> g_stopRequested{false};

#ifdef _WIN32
BOOL WINAPI onConsoleCtrl(DWORD /*type*/)
{
    g_stopRequested = true;
    if (g_svr) {
        g_svr->stop(); // 让阻塞中的 listen() 返回
    }
    return TRUE;
}
#endif

// httplib::Headers 是 multimap<string,string>（键大小写不敏感），事件里要的是
// 普通 map —— 转一下，让 Event 的类型保持简单，不依赖 httplib。
std::map<std::string, std::string> toPlainMap(const httplib::Headers &h)
{
    std::map<std::string, std::string> m;
    for (const auto &kv : h) {
        m.emplace(kv.first, kv.second);
    }
    return m;
}

// 统一的错误体：{"error":{"message":"..."}}
//
// 为什么是这个形状：OpenAI / Anthropic 的 SDK 解析错误时都走 error.message。
// 代理自己造的错误（404 没匹配、502 连不上）也套这个壳，下游就一行不用改；
// 否则每个 SDK 都得为"代理这一层"单独写一个分支。
//
// 参数带 status 是因为 404（没有上游匹配）和 502（上游连不上）都要用它，
// 而这两件事的语义完全不同：404 是"这条路径我不管"，502 是"我管但对面没答"。
void replyJsonError(httplib::Response &res, int status, const std::string &message)
{
    res.status = status;
    res.set_content(nlohmann::json{{"error", {{"message", message}}}}.dump(),
                    "application/json");
}

    // 一行请求日志。
    //
    // 为什么要"先拼成 string 再一次 <<"，而不是连着十几个 << 打出去：
    //   handler 在线程池里并发跑，cout 的每个 << 各自加锁，但整行不是原子的 ——
    //   两个请求同时打，输出会交错成半行 + 半行，日志没法看。
    //   先 ostringstream 拼好，最后只做**一次** operator<<，交错窗口缩到最小。
    //
    // 这也是 T3.3 那个 Recorder 的雏形：先在锁外把行拼完整，再一口气写出去。
    // 锁只保证"行是完整的"，不保证"顺序和你想象的一样"（并发下顺序本来就无意义）。
    //
    // dest：这次请求发去了哪（"http://host/pathent"）。404 这类没有目标的传 "-"。
    // ms  ：耗时。传负数表示不打印（比如 404 根本没发出去，耗时没有意义）。
    void logProxy(const httplib::Request &req, const std::string &dest, int status,
                  long long ms = -1)
    {
        std::ostringstream line;
        line << "[" << req.method << "] " << req.path << " -> " << dest << "  " << status;
        if (ms >= 0)
        {
            line << "  " << ms << "ms";
        }
        line << "\n";
        std::cout << line.str();
    }

} // namespace

namespace mxr
{

    // ═══ T3.2 填空（四）：挑要带给上游的 header ══════════════════════
    // 只挑三样，其余一律不带：
    //   Content-Type   —— 不带的话上游会把 JSON body 当表单或纯文本，直接解析失败
    //   Authorization  —— 不带的话上游直接 401，业务全挂
    //   Accept         —— 客户端可能只要 SSE（text/event-stream），丢了就变了语义
    // 【不能】带 Host / Content-Length：前者要指向上游，后者要按实际 body 长度重算，
    //        这两个交给 httplib::Client 自己填。
    // 【不能】带 Connection / Transfer-Encoding / Keep-Alive：hop-by-hop header，
    //        只管客户端到代理这一跳，搬到上游那一跳就是错的。
    //
    // 用得到：
    //   req.get_header_value("Content-Type")   找不到返回空串 ""（不会抛）
    //   headers.emplace("Content-Type", v)     往 httplib::Headers（multimap）里塞
    //
    // 样例：请求带 Content-Type: application/json + Authorization: Bearer sk-xxx
    //       → 返回的 Headers 里正好这两条，条数 2。
    static httplib::Headers pickRequestHeaders(const httplib::Request &req)
    {
        httplib::Headers headers;
        std::string v = req.get_header_value("Content-Type");
        if (!v.empty())
            headers.emplace("Content-Type", v);
        v = req.get_header_value("Authorization");
        if (!v.empty())
            headers.emplace("Authorization", v);
        v = req.get_header_value("Accept");
        if (!v.empty())
            headers.emplace("Accept", v);
        return headers;
    }

    // ═══ T3.2 填空（三）：handler 主体 ═══════════════════════════════
    // 按顺序做六件事（每步失败都要提前返回，别让它继续往下跑）：
    //
    //   1. auto rt = resolve(cfg.proxy.upstreams, req.path);
    //      没命中 → res.status = 404，回一段 JSON 说明"没有上游匹配这条路径"，
    //      然后 return Handled。
    //
    //   2. auto target = buildTarget(*rt->upstream, rt->path);
    //      拆不出 origin（配置 url 写坏了）→ replyBadGateway(res, "bad upstream url")，
    //      return Handled。
    //
    //   3. 记开始时间：const auto t0 = std::chrono::steady_clock::now();
    //
    //   4. 建连接：httplib::Client cli(target->origin);
    //      然后 cli.set_connection_timeout(3, 0);   // 3 秒，别让一个死上游挂住工作线程
    //      —— 这里是"每请求一个 Client"，没有连接复用。M4 会因为这个栽跟头，届时再改。
    //
    //   5. 组一个要发出去的请求（注意是 httplib::Request 的另一个副本，不是外面那个）：
    //        httplib::Request out;
    //        out.method = req.method;      // GET/POST 原样带过去
    //        out.path   = target->path;    // 剥完前缀拼好的路径
    //        out.body   = req.body;        // body 一个字节都不改
    //        out.headers = pickRequestHeaders(req);
    //      然后 auto r = cli.send(out);
    //
    //   6. 看结果：
    //      - !r（连不上、超时、DNS 失败）：replyBadGateway(res,
    //            httplib::to_string(r.error()))；reason 里给的是人话
    //      - r 有值：res.status = r->status;   **状态码必须原样搬**
    //                Content-Type 用 r->get_header_value("Content-Type")，
    //                拿到非空才 res.set_content(r->body, ct)；拿到空就 res.body = r->body
    //                （空 content_type 塞进 set_content 会写出一个空的 Content-Type 头）
    //
    //   最后打一行日志，格式随意但要能一眼看出谁、去哪、多快：
    //      std::cout << req.method << " " << req.path << " -> " << target->origin
    //                << target->path << " " << res.status << " " << ms << "ms\n";
    //   耗时 = duration_cast<milliseconds>(steady_clock::now() - t0).count()
    //
    // 注意：这个 lambda 在线程池的工作线程里跑，多个请求会并发进来。
    // 这一关没有共享可变状态，所以不用加锁；T3.3 加 Recorder 时就有了，那时再说。

    // ═══ T3.3：一次转发 = 一个函数 ═══════════════════════════════════
    // T3.2 时这段逻辑长在 handler 里，一路 return 到底。加记录之后不能这么写了，
    // 所以把它单独提出来：它只负责"发出去 + 把结果搬回 res"，不管路由、不管记录。
    static void forwardTo(const Target &target, const httplib::Request &req,
                          const httplib::Headers &fwdHeaders, httplib::Response &res)
    {
        // 每请求一个 Client：连、发、断。缺点很明显（每次三次握手），M4 再说。
        httplib::Client cli(target.origin);
        cli.set_connection_timeout(3, 0);

        httplib::Request out;
        out.method = req.method; // GET/POST 原样带过去
        out.path = target.path;  // 剥完前缀拼好的路径（含 query）
        out.body = req.body;     // body 一个字节都不改
        out.headers = fwdHeaders;

        auto r = cli.send(out);
        if (!r)
        {
            replyJsonError(res, 502,
                           "minixray: upstream error: " + httplib::to_string(r.error()));
            return;
        }
        // 状态码必须原样搬：上游 429，你转成 200，下游 SDK 的重试退避就废了
        res.status = r->status;
        const std::string ct = r->get_header_value("Content-Type");
        if (ct.empty())
        {
            // 空 content_type 塞进 set_content 会写出一个空的 Content-Type 头
            res.body = r->body;
        }
        else
        {
            res.set_content(r->body, ct);
        }
    }

    // ═══ T3.3：handler 的三段式 ═════════════════════════════════════
    //
    // 为什么不能沿用"一路 return"：
    //   每个提前 return 都得配一条 resp 事件，漏一个就是"有 req 没 resp"的孤儿，
    //   M5 会把它当成"请求没回来"。靠人记住"这里也要记"迟早会漏。
    //
    // 所以拆成三段，中间那段**只写 res、不 return**：
    //   ① 路由（纯字符串）      → 立刻 push req 事件
    //   ② 决定响应（404/502/转发）→ 只改 res
    //   ③ 无条件 push resp 事件
    //
    // 这样"成对"是结构保证的，不是纪律保证的。
    int serve(const Config &cfg)
    {
        httplib::Server svr;

        // ── 超时 ──────────────────────────────────────────────────
        // httplib 默认读/写超时各只有 5 秒（CPPHTTPLIB_READ_TIMEOUT_SECOND）。
        // "读超时"指的是：连接建立后，等对端下一个字节最多能等多久 —— 注意它
        // 不是"整个请求最长多久"，而是两个字节之间。上游思考 6 秒、或者 SSE
        // 两段 chunk 之间停了 5 秒，库就会自己掐断，报的是 Error::Read。
        // 那看起来像"上游挂了"，其实是"我们等不及"。代理的职责是把下游愿意等
        // 的时间原样花出去，所以这里直接放到 300 秒。
        // 它不影响连不上的判定：connect 超时仍然由 Client 的 3 秒管。
        svr.set_read_timeout(300, 0);
        svr.set_write_timeout(300, 0);

        // ── run 目录 + 记录器 ─────────────────────────────────────
        // 每次启动一个目录：<root>/runs/20261114-153012/events.jsonl
        // 为什么按"一次启动"分目录而不是写同一个文件：一次会话 = 一条链路，
        // M5 还原、M6 出报告都是以 run 为单位的；混在一起就得靠时间戳切分。
        const std::string runDir = cfg.root + "/runs/" + stampNow();
        std::error_code ec;
        std::filesystem::create_directories(runDir, ec);
        if (ec)
        {
            std::cerr << "fatal: 建不了 run 目录 " << runDir << ": " << ec.message() << "\n";
            return 1;
        }
        // Recorder 必须活到 listen 返回之后：它是 handler 捕获的引用，
        // 而且析构要负责把队列写空。放在 serve 的栈上就自动满足这两条。
        Recorder rec(runDir + "/events.jsonl");
        std::atomic<long long> hopSeq{0};

        using Handler = std::function<void(const httplib::Request &, httplib::Response &)>;
        Handler handler = [&cfg, &rec, &hopSeq](const httplib::Request &req, httplib::Response &res)
        {
            const std::string hop = "h-" + std::to_string(++hopSeq);
            const httplib::Headers fwdHeaders = pickRequestHeaders(req);

            // ── ① 路由：纯字符串，没有 I/O
            auto rt = resolve(cfg.proxy.upstreams, req.target);
            std::optional<Target> target;
            if (rt)
            {
                target = buildTarget(*rt->upstream, rt->path);
            }
            const std::string dest = target ? (target->origin + target->path) : std::string("-");

            // ── ② req 事件：此刻 method/path/body/目标都已知，立刻入队
            //    注意 body 存的是**原始值**，脱敏留给写线程 —— 热路径上不跑正则
            Event reqEv;
            reqEv.type = "req";
            reqEv.hop = hop;
            reqEv.tsMs = nowMs();
            reqEv.method = req.method;
            reqEv.path = req.target;
            reqEv.upstream = dest;
            reqEv.headers = toPlainMap(fwdHeaders);
            reqEv.body = req.body;
            rec.push(std::move(reqEv));

            // ── ③ 决定响应：只写 res，不 return（成对要靠结构，不靠记性）
            long long ms = -1;
            if (!rt)
            {
                replyJsonError(res, 404, "minixray: no upstream matched " + req.target);
            }
            else if (!target)
            {
                replyJsonError(res, 502, "minixray: bad upstream url: " + rt->upstream->url);
            }
            else
            {
                // 耗时用 steady_clock：它只往前走，不受系统时间被改影响
                const auto t0 = std::chrono::steady_clock::now();
                forwardTo(*target, req, fwdHeaders, res);
                ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - t0)
                         .count();
            }

            // ── ④ resp 事件：不管上面走了哪条分支，都必须发这一条
            Event respEv;
            respEv.type = "resp";
            respEv.hop = hop;
            respEv.tsMs = nowMs();
            respEv.method = req.method;
            respEv.path = req.target;
            respEv.status = res.status;
            respEv.latencyMs = ms;
            respEv.body = res.body;
            rec.push(std::move(respEv));

            logProxy(req, dest, res.status, ms);
        };
        svr.Post(R"(/.*)", handler);
        svr.Get(R"(/.*)", handler);

        std::cout << "mxr proxy listening on 0.0.0.0:" << cfg.port << "\n"
                  << "  upstreams: " << cfg.proxy.upstreams.size() << " 条\n";
        for (const auto &u : cfg.proxy.upstreams)
        {
            std::cout << "    " << u.match << "  ->  " << u.url << "  (" << u.name << ")\n";
        }
        std::cout << "  events: " << runDir << "/events.jsonl\n" << std::flush;

        g_svr = &svr;
#ifdef _WIN32
        SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
#endif

        if (!svr.listen("0.0.0.0", cfg.port))
        {
            if (!g_stopRequested.load())
            {
                std::cerr << "fatal: 无法监听端口 " << cfg.port << "（是不是被占了？）\n";
                return 1;
            }
            // 走到这里是 Ctrl+C：正常退出，但得先把队列写空再报数字
        }

        rec.flush(); // 停写线程 + 排空队列
        std::cout << "已停止：事件写入 " << rec.written() << " 条";
        if (rec.dropped() > 0)
        {
            std::cout << "，丢弃 " << rec.dropped() << " 条（磁盘跟不上）";
        }
        std::cout << "\n";
        return 0;
    }

} // namespace mxr
