// T2.4 mock 上游：假装成 OpenAI 的 /v1/chat/completions
//
// 它在 M3/M4 里的角色 —— 反向代理需要一个真实的"下游"来转发：
//   真 api.openai.com 要 key、要钱、延迟不可控；
//   这个假上游固定端口、固定响应结构、延迟可调，
//   代理转发过来时，一眼就能看出"哪些字段被改了、哪些被丢了"。
//
// 跑法（故意不退出，长期挂着当上游，Ctrl+C 结束）：
//   ./build/mock_upstream.exe 9100
//
// 输入/输出样例：
//   curl -X POST http://127.0.0.1:9100/v1/chat/completions -d '{"model":"mock-1"}'
//   → 200 + {"id":"chatcmpl-mock","object":"chat.completion","model":"mock-1",
//            "choices":[{"index":0,"message":{"role":"assistant","content":"hello from mock"},
//            "finish_reason":"stop"}],"usage":{"prompt_tokens":10,"completion_tokens":5,"total_tokens":15}}
//
//   curl -X POST 'http://127.0.0.1:9100/v1/chat/completions?delay=500' -d '{"model":"mock-1"}'
//   → 同样响应，但 curl 的 total time ≈ 0.5s
//
//   curl -X POST http://127.0.0.1:9100/v1/chat/completions -d '{"messages":[]}'
//   → 400 + {"error":"missing model field"}      ← 故意严厉：mock 的职责是放大错误

#include <httplib.h>
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

int main(int argc, char *argv[])
{
    const int port = (argc > 1) ? std::atoi(argv[1]) : 9100;

    httplib::Server svr;

    svr.Post("/v1/chat/completions",
             [](const httplib::Request &req, httplib::Response &res)
             {
                 // ① 取请求里的 model：
                 //    json in = nlohmann::json::parse(req.body);
                 //    没有 "model" 键、或它不是字符串 → res.status = 400;
                 //    res.set_content(R"({"error":"missing model field"})", "application/json");
                 //    然后 return（别忘了，不然会继续往下拼 200 响应）
                 //    成功则把字符串取到 std::string model 里：in["model"].get<std::string>()
                 nlohmann::json in = nlohmann::json::parse(req.body);
                 if (!in.contains("model") || !in["model"].is_string())
                 {
                     res.status = 400;
                     res.set_content(R"({"error":"missing model field"})", "application/json");
                     return;
                 }

                 // ② 人为延迟：?delay=500 表示睡 500 毫秒。
                 //    std::string d = req.get_param_value("delay");   // 没这个参数时返回空串
                 //    非空 → std::stoi 转数字 → std::this_thread::sleep_for(std::chrono::milliseconds(...))
                 //    注意：这里睡的是线程池的工作线程（默认池子 max(8, 核数-1) 个）。
                 //    M3 测时序时，这个睡眠就是"慢上游"的模拟。
                 std::string d = req.get_param_value("delay");
                 int delay = 0;
                 if (!d.empty())
                 {
                     delay = std::stoi(d);
                 }
                 std::this_thread::sleep_for(std::chrono::milliseconds(delay));

                 // ②.5 T4.2 起支持 SSE：body 里 "stream": true 就走流式分支。
                 //     每块之间停 200ms —— 这是"打字机效果"的来源，也是验证
                 //     代理有没有"边收边发"的唯一手段：如果代理攒完再发，
                 //     客户端会先静默 10 × 200ms，然后一次性刷出全部 10 段。
                 //
                 //     流的形状和 OpenAI 一致：
                 //       data: {"choices":[{"delta":{"content":"tok0 "}}]}\n\n
                 //       ...
                 //       data: {"choices":[{"delta":{},"finish_reason":"stop"}]}\n\n
                 //       data: [DONE]\n\n
                 //     注意每块末尾是**两个** \n（空行才是事件边界），
                 //     data: 后面有一个空格。
                 if (in.value("stream", false))
                 {
                     const std::string model = in["model"].get<std::string>();
                     res.set_chunked_content_provider(
                         "text/event-stream",
                         [model, i = 0](std::size_t, httplib::DataSink &sink) mutable
                         {
                             std::string payload;
                             if (i < 10)
                             {
                                 nlohmann::json chunk;
                                 chunk["id"] = "chatcmpl-mock";
                                 chunk["object"] = "chat.completion.chunk";
                                 chunk["model"] = model;
                                 chunk["choices"] = nlohmann::json::array();
                                 chunk["choices"].push_back(
                                     {{"index", 0},
                                      {"delta", {{"content", "tok" + std::to_string(i) + " "}}},
                                      {"finish_reason", nullptr}});
                                 payload = "data: " + chunk.dump() + "\n\n";
                                 ++i;
                                 std::this_thread::sleep_for(std::chrono::milliseconds(200));
                             }
                             else if (i == 10)
                             {
                                 nlohmann::json chunk;
                                 chunk["id"] = "chatcmpl-mock";
                                 chunk["object"] = "chat.completion.chunk";
                                 chunk["model"] = model;
                                 chunk["choices"] = nlohmann::json::array();
                                 chunk["choices"].push_back(
                                     {{"index", 0}, {"delta", nlohmann::json::object()}, {"finish_reason", "stop"}});
                                 chunk["usage"] = {{"prompt_tokens", 10},
                                                   {"completion_tokens", 10},
                                                   {"total_tokens", 20}};
                                 payload = "data: " + chunk.dump() + "\n\n";
                                 ++i;
                             }
                             else
                             {
                                 payload = "data: [DONE]\n\n";
                                 if (!sink.write(payload.data(), payload.size()))
                                 {
                                     return false;
                                 }
                                 sink.done(); // 告诉库"说完了"，否则它会一直调下去
                                 return true;
                             }
                             return sink.write(payload.data(), payload.size());
                         });
                     return;
                 }

                 // ③ OpenAI 形状的响应。四个要点：
                 //    - model 用请求里带过来的（round-trip），不是写死的
                 //    - choices 是"数组里放对象"，所以要显式 json::array() 再 push_back
                 //    - message 是"对象里的对象"，多一层花括号
                 //    - usage 里三个数字不加引号（加了就成了字符串，M3 按数字取会失败）
                 nlohmann::json out;
                 out["id"] = "chatcmpl-mock";
                 out["object"] = "chat.completion";
                 out["model"] = in["model"];

                 out["choices"] = nlohmann::json::array();
                 out["choices"].push_back({{"index", 0},
                                           {"message", {{"role", "assistant"}, {"content", "hello from mock"}}},
                                           {"finish_reason", "stop"}});

                 out["usage"] = {{"prompt_tokens", 10},
                                 {"completion_tokens", 5},
                                 {"total_tokens", 15}};

                 res.set_content(out.dump(), "application/json");
             });

    std::cout << "mock upstream listening on 127.0.0.1:" << port << "\n";
    svr.listen("0.0.0.0", port);
    return 0;
}
