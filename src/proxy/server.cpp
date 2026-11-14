#include "proxy/server.h"

#include <httplib.h>

#include <chrono>
#include <iostream>
#include <sstream>
#include <string>

#include "proxy/route.h"
#include "proxy/target.h"

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

namespace {

// 把"上游没连上"包装成 502。
// 502 Bad Gateway 的语义就是"我这个网关从上游拿到了无效响应/根本没连上"，
// 和 500（我自己的代码崩了）是两件事，不能混。
void replyBadGateway(httplib::Response &res, const std::string &why)
{
    res.status = 502;
    res.set_content("{\"error\":\"bad gateway\",\"detail\":\"" + why + "\"}",
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
    if (ms >= 0) {
        line << "  " << ms << "ms";
    }
    line << "\n";
    std::cout << line.str();
}

} // namespace

namespace mxr {

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
    std::string v= req.get_header_value("Content-Type");
    if(!v.empty())headers.emplace("Content-Type", v);
    v= req.get_header_value("Authorization");
    if(!v.empty())headers.emplace("Authorization", v);
    v= req.get_header_value("Accept");
    if(!v.empty())headers.emplace("Accept", v);
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

int serve(const Config &cfg)
{
    httplib::Server svr;

    using Handler = std::function<void(const httplib::Request &, httplib::Response &)>;
    Handler handler =[&cfg](const httplib::Request &req, httplib::Response &res)
        {
            auto rt = resolve(cfg.proxy.upstreams, req.target);//找命中上游
            if(rt==std::nullopt){
                res.status=404;
                res.set_content("Upstream Not Found", "text/plain");
                return;
            }
            auto target = buildTarget(*rt->upstream, rt->path);//解析上游url
            if(target==std::nullopt){
                replyBadGateway(res,"bad upstream url");
                return;
            }
            const auto t0 = std::chrono::steady_clock::now();
            httplib::Client cli(target->origin);
            cli.set_connection_timeout(3, 0);
             httplib::Request out;
            out.method = req.method;      // GET/POST 原样带过去
            out.path   = target->path;    // 剥完前缀拼好的路径
            out.body   = req.body;        // body 一个字节都不改
            out.headers = pickRequestHeaders(req);
            auto r = cli.send(out);
            if(!r){
                replyBadGateway(res,
                                httplib::to_string(r.error()));
                return;
            }
            res.status = r->status;
            std::string ct=r->get_header_value("Content-Type");
            if(ct.empty()){
                res.body=r->body;
            }else{
                res.set_content(r->body,ct);
            }
            // 耗时用 steady_clock：它只往前走，不受系统时间被改影响。
            // （如果要"事件发生在几点"，那才用 system_clock —— T3.3 记录时间戳时用得着）
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0)
                                .count();
            logProxy(req, target->origin + target->path, res.status, ms);
            return;

        };
    svr.Post(R"(/.*)", handler);
    svr.Get(R"(/.*)", handler);
    std::cout << "mxr proxy listening on 0.0.0.0:" << cfg.port << "\n"
              << "  upstreams: " << cfg.proxy.upstreams.size() << " 条\n";
    for (const auto &u : cfg.proxy.upstreams)
    {
        std::cout << "    " << u.match << "  ->  " << u.url << "  (" << u.name << ")\n";
    }

    if (!svr.listen("0.0.0.0", cfg.port))
    {
        std::cerr << "fatal: 无法监听端口 " << cfg.port << "（是不是被占了？）\n";
        return 1;
    }
    return 0;
}

} // namespace mxr
