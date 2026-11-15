#include "parse/extract.h"

namespace mxr
{
    namespace
    {

        using nlohmann::json;

        // 安全取值：缺键 → 默认值（不算失败）；存在但类型不符 → 默认值并计一次 parseFailed。
        // null 在 OpenAI 协议里是合法值（例如 finish_reason:null），按"没有值"处理，不报错。
        std::string getStr(const json &j, const char *key, int &failed)
        {
            if (!j.is_object() || !j.contains(key))
                return std::string();
            const auto &v = j.at(key);
            if (v.is_null())
                return std::string();
            if (v.is_string())
                return v.get<std::string>();
            ++failed;
            return std::string();
        }

        long long getNum(const json &j, const char *key, int &failed)
        {
            if (!j.is_object() || !j.contains(key))
                return 0;
            const auto &v = j.at(key);
            if (v.is_null())
                return 0;
            if (v.is_number())
                return v.get<long long>();
            ++failed;
            return 0;
        }

        bool getBool(const json &j, const char *key, int &failed)
        {
            if (!j.is_object() || !j.contains(key))
                return false;
            const auto &v = j.at(key);
            if (v.is_null())
                return false;
            if (v.is_boolean())
                return v.get<bool>();
            ++failed;
            return false;
        }

        json getObj(const json &j, const char *key, int &failed)
        {
            if (!j.is_object() || !j.contains(key))
                return json::object();
            const auto &v = j.at(key);
            if (v.is_null())
                return json::object();
            if (v.is_object())
                return v;
            ++failed;
            return json::object();
        }

        json getArr(const json &j, const char *key, int &failed)
        {
            if (!j.is_object() || !j.contains(key))
                return json::array();
            const auto &v = j.at(key);
            if (v.is_null())
                return json::array();
            if (v.is_array())
                return v;
            ++failed;
            return json::array();
        }

        // usage 的判据：键不存在 → observed=false 且三个数都是 0（"未观测"）；
        // 键存在且是对象 → observed=true，哪怕三个数真是 0（"真的是 0"）。
        Usage readUsage(const json &j, int &failed)
        {
            Usage u;
            if (!j.is_object() || !j.contains("usage"))
                return u;
            const auto &v = j.at("usage");
            if (!v.is_object())
            {
                ++failed;
                return u;
            }
            u.observed = true;
            u.promptTokens = getNum(v, "prompt_tokens", failed);
            u.completionTokens = getNum(v, "completion_tokens", failed);
            u.totalTokens = getNum(v, "total_tokens", failed);
            return u;
        }

    } // namespace

// req body → RequestInfo：认出 model / messages / tools / stream。
    RequestInfo parseRequest(const std::string &body)
    {
        RequestInfo out;
        json root = json::parse(body, nullptr, false);
        if (root.is_discarded())
        {
            out.parseFailed = 1;
            return out;
        }
        out.model = getStr(root, "model", out.parseFailed);
        out.stream = getBool(root, "stream", out.parseFailed);
        json msgs = getArr(root, "messages", out.parseFailed);
        for (const auto &m : msgs)
        {
            std::string role = getStr(m, "role", out.parseFailed);
            std::string content = getStr(m, "content", out.parseFailed);
            std::string toolCallId = getStr(m, "tool_call_id", out.parseFailed);
            std::string name = getStr(m, "name", out.parseFailed);
            out.messages.push_back({role, content, toolCallId, name});
        }
        json tools = getArr(root, "tools", out.parseFailed);
        for (const auto &t : tools)
        {
            json fn = getObj(t, "function", out.parseFailed);
            out.toolNames.push_back(getStr(fn, "name", out.parseFailed));
        }
        return out;
    }

// 非流式 resp body → ResponseInfo：content / tool_calls / finish_reason / 顶层 usage。
    ResponseInfo parseResponse(const std::string &body)
    {
        ResponseInfo out;
        json root = json::parse(body, nullptr, false);
        if (root.is_discarded())
        {
            out.parseFailed = 1;
            return out;
        }
        json choices = getArr(root, "choices", out.parseFailed);
        if (!choices.empty())
        {
            auto c0 = choices[0];
            if (c0.is_object())
            {
                json msg = getObj(c0, "message", out.parseFailed);
                out.outputText = getStr(msg, "content", out.parseFailed);
                out.finishReason = getStr(c0, "finish_reason", out.parseFailed);
                json tcs = getArr(msg, "tool_calls", out.parseFailed);
                for (const auto &tc : tcs)
                {
                    json fn = getObj(tc, "function", out.parseFailed);

                    std::string id = getStr(tc, "id", out.parseFailed),
                                name = getStr(fn, "name", out.parseFailed),
                                argsJson = getStr(fn, "arguments", out.parseFailed);
                    out.toolCalls.push_back({id, name, argsJson});
                }
            }
        }
        out.usage = readUsage(root, out.parseFailed);

        return out;
    }

// sse_end 事件 → ResponseInfo：消费 M4 累积后的结论。
    ResponseInfo parseSseEnd(const json &ev)
    {
        ResponseInfo out;
        out.outputText = getStr(ev, "text", out.parseFailed);
        out.finishReason = getStr(ev, "finish_reason", out.parseFailed);
        out.usage = readUsage(ev, out.parseFailed);
        return out;
    }

} // namespace mxr
