// T2.3 验证台：用 cpp-httplib 重写 T2.2 的服务端
//
// 和 http_server_check 跑同样的 curl，对比两边行为 ——
// 手写版删掉的每一行代码，就是"库替你做的事"清单上的一项。
//
// 跑法（3 个请求用完自动退出，方案 A）：
//   ./build/httplib_check 9001
//   curl http://127.0.0.1:9001/hello
//   curl -X POST -d '{"model":"claude"}' http://127.0.0.1:9001/v1/chat/completions
//   curl http://127.0.0.1:9001/hello        ← 第 3 个打完程序自己退出
//
// 端口故意用 9001：手写版占 9000，两个验证台能同时活着对照。

#include <httplib.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

// 已完成的请求数。handler 在库的线程池工作线程里跑，多个 handler 可能并发，
// 普通 int 跨线程读写是数据竞争（UB），所以用 atomic。
static std::atomic<int> g_served{0};

int main(int argc, char* argv[]) {
    const int port = (argc > 1) ? std::atoi(argv[1]) : 9001;

    httplib::Server svr;

    // ── 路由 A：GET /hello ─────────────────────────────
    // 输入/输出样例：
    //   curl http://127.0.0.1:9001/hello
    //   → req.method="GET", req.path="/hello", req.body=""
    //   → curl 收到 200 + {"echo":"/hello"}
    svr.Get("/hello", [](const httplib::Request& req, httplib::Response& res) {
        // ① 把 {"echo": req.path} 作为响应体 set_content 出去（Content-Type 用 application/json）。
        //    拼 JSON 用 nlohmann（T1.3 学过：写 json 对象再 dump()，别手写字符串拼接）。
        //    res.status 不用设，默认就是 200。
        nlohmann::json j;
        j["echo"] = req.path;
        res.set_content(j.dump(), "application/json");
        g_served.fetch_add(1);
    });

    // ── 路由 B：POST /v1/chat/completions ──────────────
    // 输入/输出样例：
    //   curl -X POST -d '{"model":"claude"}' http://127.0.0.1:9001/v1/chat/completions
    //   → req.body = {"model":"claude"}   （Content-Length 对应的 body 库已凑齐）
    //   → curl 收到 200 + {"received":{"model":"claude"}}
    svr.Post("/v1/chat/completions",
             [](const httplib::Request& req, httplib::Response& res) {
        // ② 把 req.body 解析成 json，包一层 {"received": <解析结果>} 再 set_content 回去。
        //    json::parse(req.body) 解析失败会抛 parse_error —— 本关不接，让它炸出来看现象。
        //    （注意读进来再吐回去，round-trip；别把 body 当字符串原样塞进 received，
        //     那样输出是 {"received":"{\"model\":...}"}，多了一层转义引号）
        nlohmann::json in = nlohmann::json::parse(req.body);
        nlohmann::json out;
        out["received"] = in;
        res.set_content(out.dump(), "application/json");
        g_served.fetch_add(1);
    });

    // listen() 是阻塞调用：直到 stop() 才返回。所以丢给一个线程跑，
    // 主线程腾出手来看计数器 —— 这就是方案 A 的骨架。
    std::thread server_thread([&svr, port]() {
        if (!svr.listen("0.0.0.0", port)) {
            std::cerr << "listen " << port << " 失败\n";
        }
    });

    // ③ 主线程：等计数攒够 3 就停服。
    //    sleep 只是"别空转烧 CPU"，不是同步手段 —— 同步靠的是 atomic。
    //    stop 之后必须 join，让 listen 线程体面退出（顺序反了会永久卡死）。
    while (g_served.load() < 3) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    svr.stop();
    server_thread.join();

    std::cout << "served " << g_served.load() << " requests, bye\n";
    return 0;
}
