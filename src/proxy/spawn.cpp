#include "proxy/spawn.h"

#include <algorithm>
#include <cwctype>
#include <iterator>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mxr
{
    namespace
    {

#ifdef _WIN32

        // ═══ 辅助函数（已铺好，你直接用）════════════════════════════════

        // UTF-8 → 宽字符。CreateProcessW / 环境块都要宽字符。
        // 调两次 MultiByteToWideChar：第一次拿长度，第二次真转。
        std::wstring toWide(const std::string &s)
        {
            if (s.empty())
                return std::wstring();
            int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                        static_cast<int>(s.size()), nullptr, 0);
            std::wstring out(static_cast<std::size_t>(n), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                static_cast<int>(s.size()), out.data(), n);
            return out;
        }

        // MSVC 参数转义：CreateProcess 把命令行字符串按这套规则拆回 argv。
        // 含空格/引号的参数要加外层引号；引号前反斜杠翻倍；末尾反斜杠在引号前也翻倍。
        std::string quoteArg(const std::string &a)
        {
            if (a.find_first_of(" \t\"") == std::string::npos)
                return a;
            std::string out = "\"";
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                std::size_t slashes = 0;
                while (i < a.size() && a[i] == '\\')
                {
                    ++slashes;
                    ++i;
                }
                if (i == a.size())
                {
                    out.append(slashes * 2, '\\');
                    break;
                }
                if (a[i] == '"')
                {
                    out.append(slashes * 2 + 1, '\\');
                    out += '"';
                }
                else
                {
                    out.append(slashes, '\\');
                    out += a[i];
                }
            }
            out += '"';
            return out;
        }

        // Windows 变量名大小写不敏感（path == PATH），合并前先归一成大写。
        std::string upperKey(const std::wstring &key)
        {
            std::string s;
            s.reserve(key.size());
            for (wchar_t c : key)
                s.push_back(static_cast<char>(towupper(c)));
            return s;
        }

        // 合并"当前进程环境 + 覆盖项"成宽字符环境块。
        // 环境块格式：一串 "KEY=VALUE\0"，最后再补一个 \0（双 null 结尾）。
        // 覆盖项同 key 的旧条目跳过；以 '=' 开头的隐藏变量（=C: 等）不复制。
        std::wstring buildEnvBlock(const std::map<std::string, std::string> &extra)
        {
            std::map<std::string, std::string> overrides;
            for (const auto &kv : extra)
                overrides[upperKey(toWide(kv.first))] = kv.second;

            std::wstring block;
            LPWCH env = GetEnvironmentStringsW();
            if (env)
            {
                for (LPWCH p = env; *p; p += wcslen(p) + 1)
                {
                    const std::wstring entry(p);
                    if (entry[0] == L'=')
                        continue;
                    const std::size_t eq = entry.find(L'=');
                    const std::string key = (eq == std::wstring::npos)
                                                ? upperKey(entry)
                                                : upperKey(entry.substr(0, eq));
                    if (overrides.count(key))
                        continue;
                    block += entry;
                    block.push_back(L'\0');
                }
                FreeEnvironmentStringsW(env);
            }
            for (const auto &kv : overrides)
            {
                block += toWide(kv.first);
                block.push_back(L'=');
                block += toWide(kv.second);
                block.push_back(L'\0');
            }
            block.push_back(L'\0');
            return block;
        }

#endif // _WIN32

    } // namespace

    // 起子进程并注入环境变量，等待结束并返回退出码。
    bool spawnWithEnv(const std::vector<std::string> &argv,
                      const std::map<std::string, std::string> &extraEnv,
                      int &outExitCode,
                      std::string &outError)
    {
        /*①*/
        if (argv.empty())
        {
            outError = "命令为空";
            return false;
        }
        std::string cmdline;
        for (const auto &arg : argv)
        {
            if (!cmdline.empty())
            {
                cmdline += " ";
            }
            cmdline += quoteArg(arg);
        }
        std::wstring envBlock = buildEnvBlock(extraEnv);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};

        std::wstring cmdW = toWide(cmdline);
        bool ok = CreateProcessW(nullptr, cmdW.data(), nullptr, nullptr, TRUE,
                                 CREATE_UNICODE_ENVIRONMENT,
                                 (LPWSTR)envBlock.c_str(), nullptr, &si, &pi);
        if (!ok)
        {
            outError = "创建子进程失败(错误码 "
                     + std::to_string(GetLastError()) + "): " + cmdline;
            return false;
        }
        unsigned long code=0;
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        outExitCode = (int)code;
        return true;
    }

} // namespace mxr
