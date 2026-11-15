#include "proxy/route.h"

#include <algorithm>

namespace mxr
{

    // ── ① path 是否落在 prefix 这个"路径段"下面 ──────────────────────
    // 判据按顺序想清楚，任何一步都能直接判否：
    //   1) prefix 比 path 还长 → 不可能
    //   2) 前 prefix.size() 个字符对不上 → 不可能
    //   3) prefix 是 "/" → 根前缀，全匹配，直接真
    //   4) 长度刚好相等（path 就是 prefix 本身）→ 真
    //   5) 剩下的第一个字符是 '/' 或 '?' → 真；是别的（'b'、'0'、'x'…）→ 假
    //      为什么 '?' 也算："/v1?x=1" 里 query 从 '?' 开始，路径部分就是 "/v1" 本身，
    //      它该归 "/v1" 管。漏了这一条，GET /v1?x=1 会掉进 404。
    // 最后一步就是"路径段边界"和"字符串前缀"的全部区别，别用 starts_with 糊过去。
    bool matchSegment(const std::string &prefix, const std::string &path)
    {
        size_t n = prefix.size(), m = path.size();
        if (n > m)
            return false;
        if (n == 1 && prefix[0] == '/')
        {
            return true;
        }
        int p = 0;
        while (p < n)
        {
            if (prefix[p] != path[p])
            {
                return false;
            }
            ++p;
        }
        if (p < m && (path[p] != '/' && path[p] != '?'))
            return false;
        return true;
    }

    // ── ② 剥掉 prefix ────────────────────────────────────────────────
    // 输入保证 matchSegment(prefix, path) 为真。
    // 朴素做法是从 prefix.size() 处 substr 到末尾，然后处理两个尾巴问题：
    //   - 结果为空（path 恰好等于 prefix）→ 返回 "/"，绝不能返回 ""。
    //     转发层拼的是 url + path，空串会得到 ".../v1" 这种"少了请求路径"的请求。
    //   - 结果不以 '/' 开头（prefix 是 "/" 时会出现）→ 前面补一个 '/'。
    // 例：stripPrefix("/", "/v1/chat") → substr 从 1 开始得到 "v1/chat" → 补成 "/v1/chat"。
    std::string stripPrefix(const std::string &prefix, const std::string &path)
    {
        int n = prefix.size(), m = path.size();
        if (n == 1)
        {
            return path;
        }
        else if (n == m)
        {
            return "/";
        }
        std::string ans = path.substr(n, m - n);
        if (ans[0] == '?')
        {
            ans.insert(ans.begin(), '/');
        }
        return ans;
    }

    // ── ③ 找最长命中的上游 ──────────────────────────────────────────
    // 一次遍历就够，不要排序也不要拷贝配置：
    //   遍历 ups，对每条调 matchSegment(u.match, path)；
    //   命中就记下"目前为止 match 最长的那条"（比较 u.match.size()）。
    // 循环结束后：没记下任何一条 → return std::nullopt；
    //             记下了 → return RouteResult{ 那条的地址, stripPrefix(那条->match, path) }。
    // 为什么取最长：配置里可能同时有 "/v1" 和 "/v1/chat"，后者更具体，必须优先。
    // 两条 match 完全相同的情况由 validateUpstreams 在启动时挡掉，这里不用管平局。
    // 取地址用 &u（u 是 const 引用绑到 vector 元素上，地址就是配置里那条的地址）。
    std::optional<RouteResult> resolve(const std::vector<Upstream> &ups, const std::string &path)
    {
        RouteResult res;
        size_t max_size = 0;

        for (const auto &u : ups)
        {
            if (matchSegment(u.match, path) && u.match.size() > max_size)
            {
                max_size = u.match.size();
                res.upstream = &u;
            }
        }
        if (res.upstream == nullptr)
            return std::nullopt;
        res.path = stripPrefix(res.upstream->match, path);
        return res;
    }

    std::optional<std::string> validateUpstreams(const std::vector<Upstream> &ups)
    {
        int n = ups.size();
        for (int i = 0; i < n; i++)
        {
            std::string match = ups[i].match;
            if (match.empty())
            {
                return "upstream match is empty: " + ups[i].name;
            }
            if (ups[i].url.empty())
            {
                return "upstream url is empty: " + ups[i].name;
            }
            if (match[0] != '/')
            {
                return "match must start with '/': " + match;
            }
            if (match != "/" && match[match.size() - 1] == '/')
            {
                return "match must not end with '/': " + match;
            }
            for (int j = i + 1; j < n; j++)
            {
                if (match == ups[j].match)
                {
                    return "duplicate match: " + ups[i].name;
                }
            }
        }
        return std::nullopt;
    }

} // namespace mxr
