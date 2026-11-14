#include "store/event.h"

namespace mxr {

// ═══ T3.3 填空（一）：Event → JSON ═══════════════════════════════
//
// 这是整个项目里第一份"对外的数据格式"。以后 M5 解析、M6 画图、你的脚本
// grep 全都要认这个形状，所以字段名一次定死。
//
// 要产出的样子（一行）：
//   {"type":"req","hop":"h-1","seq":1,"ts_ms":1763100000123,"method":"POST",
//    "path":"/v1/chat/completions",
//    "upstream":"http://127.0.0.1:9100/v1/chat/completions",
//    "status":0,"latency_ms":-1,
//    "headers":{"accept":"*/*","content-type":"application/json"},
//    "body":"{\"model\":\"mock-1\"}"}
//
// 三件容易写错的事：
//   1. status / latency_ms / seq / ts_ms 是**数字**，不要加引号。
//      写成字符串以后，M6 按 latency 排序会得到 "10" < "9" 这种结果。
//   2. headers 是**对象**，std::map<std::string,std::string> 可以直接赋给 json，
//      不用手工遍历。
//   3. 返回值必须是 `j.dump()`，不是 `j`。函数签名返回 nlohmann::json，
//      而你真正想要的是"一条紧凑的 JSON 文本"—— 这个转换发生在
//      JsonlWriter::append（它调用 dump()）。所以这里返回的是 json 对象本身，
//      保持"函数返回 json、写盘时才 dump"的分工。
//
// 用法（T1.3 踩过的坑，这里再过一遍）：
//   nlohmann::json j;
//   j["type"] = ev.type;         // 写入点自动建键，不需要先造结构
//   j["headers"] = ev.headers;   // map<string,string> → JSON object
//   return j;
nlohmann::json toJson(const Event& ev)
{
    nlohmann::json j;
    j["type"] = ev.type;
    j["hop"] = ev.hop;
    j["seq"] = ev.seq;
    j["ts_ms"] = ev.tsMs;
    j["method"] = ev.method;
    j["path"] = ev.path;
    j["upstream"] = ev.upstream;
    j["status"] = ev.status;
    j["latency_ms"] = ev.latencyMs;
    j["headers"] = ev.headers;
    j["body"] = ev.body;
    return j;
}

} // namespace mxr
