#include "store/redact.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace mxr {
namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 两个模式编译一次就够，别每次调用都构造 std::regex —— 构造正则比匹配本身贵得多。
// 写成函数内 static 而不是全局变量，是因为"函数内 static 的初始化"由编译器保证
// 线程安全（C++11 起叫 magic statics），而全局对象的初始化顺序在多文件之间没有保证。
//
//   sk-[A-Za-z0-9_\-]{10,}      OpenAI 的 key：sk-proj-xxxx / sk-ant-xxxx
//   Bearer\s+[A-Za-z0-9._\-]+   Authorization: Bearer xxx 里的那段（也常出现在 body）
//
// 注意 sk- 那条用了 {10,}：少于 10 位的不动。否则像 "sk-1" 这种出现在普通文本里的
// 片段也会被打码，日志就没法看了。
const std::regex& reSk()
{
    static const std::regex re(R"(sk-[A-Za-z0-9_\-]{10,})");
    return re;
}

const std::regex& reBearer()
{
    static const std::regex re(R"(Bearer\s+[A-Za-z0-9._\-]+)");
    return re;
}

} // namespace

std::string redactBody(const std::string& body)
{
    //处理sk
    std::string res=body;
    if(body.find("sk-")!=std::string::npos){
        res=std::regex_replace(body,reSk(),"sk-***");
    }
    
    if(body.find("Bearer")!=std::string::npos){
        res=std::regex_replace(res,reBearer(),"Bearer ***");
    }
    return res;
}

bool shouldMaskHeader(const std::string& name)
{
    const std::string k = lower(name);
    return k == "authorization" || k == "proxy-authorization" || k == "x-api-key" ||
           k == "api-key" || k == "openai-api-key" || k == "anthropic-api-key";
}

std::map<std::string, std::string> redactHeaders(const std::map<std::string, std::string>& in)
{
    std::map<std::string, std::string> out;
    for (const auto& kv : in) {
        out.emplace(lower(kv.first), shouldMaskHeader(kv.first) ? "***" : kv.second);
    }
    return out;
}

} // namespace mxr
