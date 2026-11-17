#include "ui/report.h"

#include <cstdio>
#include <string>

namespace mxr
{
    namespace
    {

        constexpr std::size_t kBriefLimit = 200;

        std::string fmtMs(long long ms)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2fs", static_cast<double>(ms) / 1000.0);
            return std::string(buf);
        }

        // 表格单元格转义：| 会破列、反引号会开代码、换行会断表格行。
        std::string cell(const std::string &s)
        {
            std::string out;
            out.reserve(s.size() + 16);
            for (char c : s)
            {
                switch (c)
                {
                case '|': out += "\\|"; break;
                case '`': out += "\\`"; break;
                case '\r': break;
                case '\n': out += "<br/>"; break;
                default: out.push_back(c); break;
                }
            }
            return out;
        }

        // 截断 + 折叠：先截断再转义，避免转义产生的实体被算进 200 字符。
        std::string brief(const std::string &s)
        {
            std::string oneLine;
            oneLine.reserve(s.size());
            for (char c : s)
            {
                if (c == '\n' || c == '\r')
                {
                    if (oneLine.empty() || oneLine.back() != ' ') oneLine.push_back(' ');
                }
                else
                {
                    oneLine.push_back(c);
                }
            }
            return oneLine.size() > kBriefLimit
                       ? oneLine.substr(0, kBriefLimit) + "..."
                       : oneLine;
        }

        std::string tokenText(const Usage &u)
        {
            return u.observed ? std::to_string(u.totalTokens) + " tok" : "—";
        }

        std::string toolStatus(ErrorKind e)
        {
            switch (e)
            {
            case ErrorKind::confirmed: return "失败";
            case ErrorKind::inferred:  return "疑似失败";
            case ErrorKind::none:      return "成功";
            }
            return "—";
        }

        // 最早出问题的步：未完成，或有失败/疑似失败的工具。
        int firstFailureIndex(const Run &run)
        {
            for (std::size_t i = 0; i < run.steps.size(); ++i)
            {
                const auto &s = run.steps[i];
                if (!s.complete) return static_cast<int>(i);
                for (const auto &t : s.tools)
                    if (t.error != ErrorKind::none) return static_cast<int>(i);
            }
            return -1;
        }

        std::string failureReason(const Step &s)
        {
            if (!s.complete) return "该步未完成（流被中断）";
            for (const auto &t : s.tools)
            {
                if (t.error == ErrorKind::confirmed)
                    return "工具 `" + t.name + "` 明确失败";
                if (t.error == ErrorKind::inferred)
                    return "工具 `" + t.name + "` 结果疑似失败";
            }
            return "—";
        }

    } // namespace

    std::string emitReport(const Run &run)
    {
        std::string out;
        out += "# 执行报告\n\n";

        const int failAt = firstFailureIndex(run);
        if (failAt < 0)
        {
            out += "**结论**：成功，" + std::to_string(run.steps.size()) + " 步全部完成。\n\n";
        }
        else
        {
            out += "**结论**：第 " + std::to_string(failAt)
                   + " 步开始出问题 — " + failureReason(run.steps[failAt]) + "。\n\n";
        }

        long long stepCount = 0, toolCount = 0, totalTokens = 0;
        for (const auto &s : run.steps)
        {
            ++stepCount;
            totalTokens += s.usage.totalTokens;
            toolCount += static_cast<long long>(s.tools.size());
        }

        out += "## 概览\n\n";
        out += "| 项目 | 值 |\n|---|---|\n";
        out += "| Run 数 | 1 |\n";
        out += "| 步数 | " + std::to_string(stepCount) + " |\n";
        out += "| 工具调用 | " + std::to_string(toolCount) + " |\n";
        out += "| 总耗时 | " + fmtMs(run.endMs - run.startMs) + " |\n";
        out += "| 总 token | " + (stepCount ? std::to_string(totalTokens) : "—") + " |\n\n";

        if (failAt >= 0)
        {
            const auto &s = run.steps[failAt];
            out += "## 最早失败\n\n";
            out += "- 位于：第 " + std::to_string(failAt) + " 步\n";
            out += "- 模型：" + (s.model.empty() ? "—" : cell(s.model)) + "\n";
            out += "- 原因：" + failureReason(s) + "\n";
            out += "- 输出摘要：" + (s.outputText.empty() ? "—" : cell(brief(s.outputText))) + "\n\n";
        }

        for (std::size_t i = 0; i < run.steps.size(); ++i)
        {
            const auto &s = run.steps[i];
            out += "## Step " + std::to_string(i) + " · "
                   + (s.model.empty() ? "—" : cell(s.model)) + "\n\n";
            out += "- 模型：" + (s.model.empty() ? "—" : cell(s.model)) + "\n";
            out += "- 耗时：" + fmtMs(s.endMs - s.startMs) + "\n";
            out += "- token：" + tokenText(s.usage) + "\n";
            out += "- 输出摘要：" + (s.outputText.empty() ? "—" : cell(brief(s.outputText))) + "\n";
            out += "- 状态：" + std::string(s.complete ? "完成" : "未完成") + "\n\n";

            if (s.tools.empty())
            {
                out += "工具：—\n\n";
                continue;
            }

            out += "| 工具 | 耗时 | 结果摘要 | 状态 |\n|---|---|---|---|\n";
            for (const auto &t : s.tools)
            {
                out += "| " + cell(t.name.empty() ? "—" : t.name)
                       + " | " + fmtMs(t.durationMs) + (t.durationInferred ? " ~" : "")
                       + " | " + cell(t.resultJson.empty() ? "—" : brief(t.resultJson))
                       + " | " + toolStatus(t.error) + " |\n";
            }
            out += "\n";
        }

        return out;
    }

} // namespace mxr
