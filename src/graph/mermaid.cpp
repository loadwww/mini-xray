#include "graph/mermaid.h"

#include <cstdio>
#include <string>

namespace mxr
{
    namespace
    {

        // 毫秒 → "1.05s"。Mermaid 标签里放原始毫秒数可读性差。
        std::string fmtMs(long long ms)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2fs", static_cast<double>(ms) / 1000.0);
            return std::string(buf);
        }

        // 节点 id：只含字母数字下划线，与显示标签分离。
        std::string stepId(int i) { return "s" + std::to_string(i); }
        std::string toolId(int i, int j)
        {
            return "t" + std::to_string(i) + "_" + std::to_string(j);
        }

        // token 未观测（usage.observed=false）时显示破折号，不显示误导性的 0。
        std::string tokenText(const Usage &u)
        {
            return u.observed ? std::to_string(u.totalTokens) + " tok" : "token —";
        }

    } // namespace

    // 逐字符转义标签里的 & " < > 换行与回车。
    std::string escapeMermaidLabel(const std::string &s)
    {
        std::string out;
        out.reserve(s.size() + 16);
        for (char c : s)
        {
            switch (c)
            {
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '\n': out += "<br/>"; break;
            case '\r': break;
            default: out.push_back(c); break;
            }
        }
        return out;
    }

    // 生成 flowchart TD：Step/工具节点、顺序与扇出边、失败分级样式。
    std::string emitMermaid(const Run &run)
    {
        std::string out = "flowchart TD\n";

        for (std::size_t i = 0; i < run.steps.size(); ++i)
        {
            const auto &s = run.steps[i];
            std::string label = "Step " + std::to_string(i) + " · " + s.model
                                + " · " + fmtMs(s.endMs - s.startMs)
                                + " · " + tokenText(s.usage);
            out += "    " + stepId((int)i) + "[\"" + escapeMermaidLabel(label) + "\"]\n";
            for (std::size_t j = 0; j < s.tools.size(); ++j)
            {
                const auto &t = s.tools[j];
                std::string tl = t.name + " · " + fmtMs(t.durationMs)
                                 + (t.durationInferred ? " ~" : "");
                out += "    " + toolId((int)i, (int)j) + "[\""
                       + escapeMermaidLabel(tl) + "\"]\n";
            }
        }

        for (std::size_t i = 0; i < run.steps.size(); ++i)
        {
            const auto &s = run.steps[i];
            for (std::size_t j = 0; j < s.tools.size(); ++j)
                out += "    " + stepId((int)i) + " --> " + toolId((int)i, (int)j) + "\n";

            if (i + 1 < run.steps.size())
            {
                // 有工具的步：工具结果回来才进入下一步 → 从工具汇聚到 s(i+1)，
                // 避免 Mermaid 把下一步和并行工具排成同一层（见口试第 5 题）。
                if (!s.tools.empty())
                    for (std::size_t j = 0; j < s.tools.size(); ++j)
                        out += "    " + toolId((int)i, (int)j) + " --> "
                               + stepId((int)(i + 1)) + "\n";
                else
                    out += "    " + stepId((int)i) + " --> " + stepId((int)(i + 1)) + "\n";
            }
        }

        bool hasFailed = false;
        for (const auto &s : run.steps)
            for (const auto &t : s.tools)
                if (t.error != ErrorKind::none) hasFailed = true;
        if (hasFailed)
        {
            out += "    classDef failed fill:#f96,stroke:#333\n";
            out += "    classDef warn fill:#f8c471,stroke:#333\n";
            for (std::size_t i = 0; i < run.steps.size(); ++i)
                for (std::size_t j = 0; j < run.steps[i].tools.size(); ++j)
                {
                    auto e = run.steps[i].tools[j].error;
                    if (e == ErrorKind::none) continue;
                    out += "    class " + toolId((int)i, (int)j)
                           + (e == ErrorKind::confirmed ? " failed\n" : " warn\n");
                }
        }
        return out;
    }

    // 生成相对时间轴的 gantt：Step 区间与并行工具条。
    std::string emitTimeline(const Run &run)
    {
        std::string out = "gantt\ntitle Execution timeline\ndateFormat x\naxisFormat %S\n";
        for (std::size_t i = 0; i < run.steps.size(); ++i)
        {
            const auto &s = run.steps[i];
            out += "    section Step " + std::to_string(i) + "\n";
            out += "    " + stepId((int)i) + " :"
                   + (s.complete ? "done" : "active") + ", "
                   + std::to_string(s.startMs - run.startMs) + ", "
                   + std::to_string(s.endMs - run.startMs) + "\n";
            for (std::size_t j = 0; j < s.tools.size(); ++j)
            {
                const auto &t = s.tools[j];
                out += "    " + escapeMermaidLabel(t.name) + " :"
                       + (t.error == ErrorKind::none ? "done" : "crit") + ", "
                       + std::to_string(t.startMs - run.startMs) + ", "
                       + std::to_string(t.startMs + t.durationMs - run.startMs) + "\n";
            }
        }
        return out;
    }

} // namespace mxr
