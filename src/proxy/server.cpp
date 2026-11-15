#include "proxy/server.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "common/clock.h"
#include "proxy/route.h"
#include "proxy/stream_bridge.h"
#include "proxy/target.h"
#include "store/event.h"
#include "store/recorder.h"
#include "store/sse_run.h"

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
        if (g_svr)
        {
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
        for (const auto &kv : h)
        {
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
        // 必须 flush：std::cout 连终端时是行缓冲（每个 \n 都刷），但连管道/文件时
        // 是**全缓冲**（攒满约 4KB 才刷）。日志重定向到文件、或者被 tools 捕获时，
        // 不 flush 就意味着"请求早就处理完了，日志过几十条才出现"—— 排查现场时
        // 最需要的就是这一刻的输出。代价是一次 write 系统调用，量级是微秒。
        std::cout << line.str() << std::flush;
    }

} // namespace

namespace mxr
{

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

    // 判 Content-Type 是不是 SSE。用 rfind(...,0) == 0 而不是 substr 比较：
    // 前者是"前缀比较"，不构造临时 string。上游常写成
    // "text/event-stream; charset=utf-8"，所以必须按前缀比，不能全等。
    static bool isEventStream(const std::string &ct)
    {
        return ct.rfind("text/event-stream", 0) == 0;
    }

    // T4.4：多两个参数。为什么必须传进来：
    //   provider 会在 handler **返回之后**还被库反复调用（边收边发），
    //   那时 handler 的局部变量 hop 已经销毁。所以 hop 要按值传、再由 provider
    //   按值捕获；rec 引用 serve() 栈上的 Recorder，它活得比所有连接久。
    //   反过来，如果在这里按引用捕获 handler 的 hop，就是典型的悬垂引用。
    static void forwardStreaming(const Target &target, const httplib::Request &req,
                                 const httplib::Headers &fwdHeaders, httplib::Response &res,
                                 Recorder &rec, const std::string &hop)
    {
        auto bridge = std::make_shared<StreamBridge>();

        httplib::Request out;
        out.method = req.method; // GET/POST 原样带过去
        out.path = target.path;  // 剥完前缀拼好的路径（含 query）
        out.body = req.body;     // body 一个字节都不改
        out.headers = fwdHeaders;

        out.content_receiver = [bridge](const char *data, std::size_t len,
                                        std::uint64_t /*offset*/, std::uint64_t /*total*/) -> bool
        {
            return bridge->push(data, len);
        };

        // 响应头到了（状态行 + header 已读，body 还没开始）。
        // 这是唯一能"在 body 之前"拿到状态码和 Content-Type 的时机，
        // 而 handler 必须在那之前决定怎么回。所以这个信号必须传出去。
        out.response_handler = [bridge](const httplib::Response &r) -> bool
        {
            bridge->setHead(r.status, r.get_header_value("Content-Type"));
            return true; // false 会中断，这里没有理由中断
        };

        // 起线程。注意 out 是**移动**进去的：它含 lambda，含 shared_ptr，
        // 拷一份没有意义，而且 send() 内部本来也会再拷一次。
        // detach 而不是 join：handler 不能等它（等了就变回同步），
        // 而它会自己结束（上游说完 / 出错 / 被 abort）。
        // 安全靠 shared_ptr：线程和 provider 各持一份 bridge，谁先走都不会悬空。
        std::thread worker([bridge, out = std::move(out), origin = target.origin]() mutable
                           {
                               httplib::Client cli(origin);
                               cli.set_connection_timeout(3, 0);
                               // 读超时是"两个字节之间最多等多久"，不是"整条流最长多久"。
                               // 默认 5 秒，SSE 思考久一点就被库自己掐断，报的还是
                               // Error::Read（看起来像上游挂了）。和 server 侧对齐到 300 秒。
                               cli.set_read_timeout(300, 0);
                               auto r = cli.send(out);
                               if (r) {
                                   bridge->finish();
                               } else {
                                   bridge->fail(httplib::to_string(r.error()));
                               } });
        worker.detach();

        // ── 等头 ─────────────────────────────────────────────────
        // 5 秒：connect 超时 3 秒，再加 2 秒给上游组织响应头。再久就该告诉
        // 客户端"网关超时"，而不是让它一直挂着。
        if (!bridge->waitHead(5000))
        {
            bridge->abort(); // 让还卡在连接里的上游线程收工
            replyJsonError(res, 504, "minixray: upstream timeout (no response head)");
            return;
        }
        if (bridge->failed())
        {
            // 头都没拿到就失败 = 连不上（502）。这和"上游回了 500"是两件事：
            // 后者是我们成功了、上游错了，状态码要原样搬。
            replyJsonError(res, 502, "minixray: upstream error: " + bridge->error());
            return;
        }

        // 状态码必须原样搬：上游 429，你转成 200，下游 SDK 的重试退避就废了
        res.status = bridge->status();
        const std::string ct = bridge->contentType();

        // ── 非流式：收完再回 ──────────────────────────────────────
        // 和 T3.2 的行为逐字一致（body、Content-Type、状态码），
        // 唯一区别是数据先经过队列。这也是"一个函数两种模式"的价值：
        // 掉进队列的那条路径对普通 JSON 请求没有任何可观察的变化。
        if (!isEventStream(ct))
        {
            std::string body;
            std::string chunk;
            while (bridge->next(chunk))
            {
                body += chunk;
            }
            if (bridge->failed())
            {
                replyJsonError(res, 502,
                               "minixray: upstream error: " + bridge->error());
                return;
            }
            if (ct.empty())
            {
                res.body = std::move(body); // 空 content_type 塞进 set_content 会写出空头
            }
            else
            {
                res.set_content(body, ct);
            }
            return;
        }

        // ── 流式：边收边发 ────────────────────────────────────────
        // 告诉中间层别缓存。httplib 自己会加 Transfer-Encoding: chunked。
        res.set_header("Cache-Control", "no-cache");

        auto run = std::make_shared<SseRunLogger>(rec, hop);

        res.set_chunked_content_provider(
            ct, [bridge, run](std::size_t /*offset*/, httplib::DataSink &sink) -> bool
            {
                if (!sink.is_writable())
                {
                    run->onEnd();
                    bridge->abort();
                    return false;
                }
                std::string chunk;
                if (bridge->next(chunk))
                {
                    run->onChunk(chunk);
                    if (!sink.write(chunk.c_str(), chunk.size()))
                    {
                        run->onEnd();
                        bridge->abort(); // 写失败 = 客户端走了，叫醒上游线程收工
                        return false;
                    }
                    return true;
                }
                run->onEnd();
                
                sink.done(); // 流说完了：写出 0\r\n\r\n，库的循环退出
                return true;
            });
}

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
            forwardStreaming(*target, req, fwdHeaders, res, rec, hop);
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
    std::cout << "  events: " << runDir << "/events.jsonl\n"
              << std::flush;

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
