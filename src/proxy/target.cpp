#include "proxy/target.h"

namespace mxr {

// ═══ T3.2 填空（一）：拆 url ══════════════════════════════════════
//
// 把 "http://127.0.0.1:9100/v1" 拆成两段：
//   origin = "http://127.0.0.1:9100"   连哪台机器（scheme + host + port）
//   base   = "/v1"                      url 里带的路径前缀
//
// 拆法（按顺序）：
//   1) 找 "://"。找不到 → 这条 url 没写 scheme，直接 return false。
//      origin 从 0 开始、长度到 "://" 之后为止（**包含** "://"）。
//   2) 从 scheme 之后开始找第一个 '/'：
//        - 找不到 → 整条 url 就是 origin，base 为空（"https://api.openai.com"）
//        - 找到了 → 前半段是 origin，从那个 '/' 到末尾是 base（"/v1"、
//                    "/v1/"、"openai/v1" 这种都可能出现，后两种下面处理）
//   3) base 结尾的 '/' 要去掉："/v1/" → "/v1"，单独的 "/" → ""。
//      **必须在拼 path 之前去掉**，否则 base + "/chat" 会变成 "//chat"。
//      注意这里只去结尾，不去开头。
//   4) origin 里如果只有 scheme 没有 host（比如 "http://"）→ return false。
//      判据：origin 的最后一个 '/' 是不是 "://" 里那个，或者 origin 的长度 <= scheme 长度。
//      简单做法：找 "://" 之后还剩多少字符，0 个就是没有 host。
//
// 用得到的东西：
//   url.find("://")            找不到返回 std::string::npos
//   url.find('/', startPos)    从 startPos 开始找
//   url.substr(pos, len)       pos 越界会抛，别让它有机会抛
//   base.back() / base.pop_back()   只在 base 非空时用
//   base.rfind(...)            从右往左找
//
// 输入 → 输出：
//   "http://127.0.0.1:9100/v1" → origin="http://127.0.0.1:9100" base="/v1"
//   "https://api.openai.com"   → origin="https://api.openai.com"  base=""
//   "https://api.openai.com/"  → origin="https://api.openai.com"  base=""
//   "127.0.0.1:9100"           → false
//   "http:///v1"               → false（没有 host）
static bool splitUrl(const std::string &url, std::string &origin, std::string &base)
{
    size_t sp1=url.find("://");
    size_t n=url.size();
    if(sp1==std::string::npos){
        return false;
    }
    int sp2=sp1+3;
    if(sp2>=n||url[sp2]=='/')return false;
    while(sp2<n&&url[sp2]!='/'){
        sp2++;
    }
    if(sp2>=n){
        origin=url;
    }else{
        origin=url.substr(0,sp2);
        base=url.substr(sp2,n-sp2);
        if(base.size()==1){
            base="";
        }else if(base[base.size()-1]=='/'){
            base.pop_back();
        }
       
    }

    return true;
}

// ═══ T3.2 填空（二）：拼目标 ══════════════════════════════════════
// 拿到 origin / base 之后，剩下的就一句：
//   origin 原样带走（httplib::Client 要的就是它）
//   path = base + strippedPath
// 因为 resolve 保证了 strippedPath 一定以 '/' 开头，而 splitUrl 已经去掉了 base 的
// 尾部 '/'，所以直接相加不会出现 "//"：
//   "/v1" + "/chat/completions" = "/v1/chat/completions"
//   ""    + "/chat/completions" = "/chat/completions"
//   "/v1" + "/"                 = "/v1/"
// 拆失败 → return std::nullopt（调用方视为"配置里这条 url 写坏了"，回 502）。
std::optional<Target> buildTarget(const Upstream &u, const std::string &strippedPath)
{
    std::string origin,base,path;
    if(!splitUrl(u.url,origin,base)){
        return std::nullopt;
    }
    std::optional<Target>res;
    path=base+strippedPath;
    Target t;
    t.origin=origin;
    t.path=path;
    res.emplace(t);
    return res;
}

} // namespace mxr
