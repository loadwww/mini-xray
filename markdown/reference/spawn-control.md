# 子进程与 Ctrl+C 原型速查（T7.3）

## 1. CreateProcessW 原型（Windows 起子进程）

```cpp
#include <windows.h>

BOOL CreateProcessW(
    LPCWSTR lpApplicationName,      // 通常 nullptr（用命令行里的程序名）
    LPWSTR  lpCommandLine,          // 完整命令行（可写缓冲，API 会改它）
    LPSECURITY_ATTRIBUTES,          // nullptr
    LPSECURITY_ATTRIBUTES,          // nullptr
    BOOL bInheritHandles,           // TRUE：继承控制台句柄（子进程输出能显示在同窗口）
    DWORD dwCreationFlags,          // CREATE_UNICODE_ENVIRONMENT（环境块是宽字符时）
    LPVOID lpEnvironment,           // 环境块；nullptr = 继承父进程环境
    LPCWSTR lpCurrentDirectory,     // nullptr = 同父进程工作目录
    LPSTARTUPINFOW lpStartupInfo,   // 必须设 cb = sizeof
    LPPROCESS_INFORMATION lpProcessInformation  // 接收句柄 + pid
);
```

**关键**：`lpCommandLine` 必须是**可写缓冲**（`std::wstring::data()` 在 C++17 返回 `wchar_t*`，可以），不能传字符串字面量——API 会原地修改它。

返回后必须 `CloseHandle(pi.hThread)` 和 `CloseHandle(pi.hProcess)`——句柄是内核对象，不关会泄漏。

## 2. 等待与取退出码

```cpp
WaitForSingleObject(pi.hProcess, INFINITE);   // 阻塞直到子进程结束
DWORD code = 0;
GetExitCodeProcess(pi.hProcess, &code);       // 取退出码（进程还没结束时会拿到 STILL_ACTIVE=259）
int childExit = static_cast<int>(code);
```

为何"子进程崩溃也要 finalize"：`WaitForSingleObject` 只要进程结束就返回，不管退出码是 0 还是崩溃码。拿到 code 后照常走收尾，最后把 code 透传成 mxr 的退出码。

## 3. MSVC 参数转义（quoteArg）

`CreateProcess` 把命令行字符串按 MSVC 规则拆回 argv。含空格的参数必须加引号，含引号的参数里 `"` 前要加 `\`，结尾连续反斜杠在引号前要翻倍。规则（对每个参数）：

```
无空格/制表符/引号 → 原样返回
否则：
  外层加一对 "
  扫描每个字符：
    连续 k 个反斜杠 + 普通字符 → 原样 k 个反斜杠 + 字符
    连续 k 个反斜杠 + "       → 2k+1 个反斜杠 + "
  末尾连续反斜杠 → 翻倍
```

例：`a b` → `"a b"`；`say "hi"` → `"say \"hi\""`。这段已铺在脚手架里，你不用重写。

## 4. 宽字符环境块（buildEnvBlock）

Windows 环境块是**一串以 `\0` 结尾的 `KEY=VALUE`，最后再补一个 `\0`**（双 null 结尾）。

```cpp
LPWCH env = GetEnvironmentStringsW();
for (LPWCH p = env; *p; p += wcslen(p) + 1) { ... }  // 遍历当前环境
FreeEnvironmentStringsW(env);
// 覆盖项：同 key 的旧条目跳过，追加新条目
// 结尾 block.push_back(L'\0');
```

**大小写不敏感**：Windows 环境变量 key 不区分大小写（`path` == `PATH`）。合并时先 `towupper` 归一再比较，否则会出现 `Path` 和 `PATH` 两条。

UTF-8 → 宽字符：`MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ...)`。调用两次——第一次传 `nullptr` 拿长度，第二次真正转换。

## 5. 环境变量注入——为什么零侵入

父进程把 `OPENAI_BASE_URL=http://127.0.0.1:8080/v1` 写进**子进程的**环境块，子进程里的 OpenAI SDK 自动读它，把请求发到代理。子进程代码一个字不用改。

对比"改代码"：要每个 agent 都硬编码代理地址，且换端口要重新编译。环境注入是进程级的运行时配置。

注意 key 的多样性：不同 SDK 读不同变量（`OPENAI_BASE_URL` / `OPENAI_API_BASE` / `DEEPSEEK_BASE_URL`），注入时要覆盖常见的几个。

## 6. Ctrl+C 的真相

Ctrl+C 不是"你的进程收到一个特殊返回值"——是 Windows 给你进程里**附加控制台的第一个进程**发 `CTRL_C_EVENT` 信号。默认行为是直接终止进程（析构函数不会执行，队列里最后几条事件丢失）。

接管方式：
```cpp
BOOL WINAPI onConsoleCtrl(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        g_stopRequested = true;
        if (g_svr) g_svr->stop();   // 让 listen() 返回
        return TRUE;                // TRUE = 我处理了，别用默认terminate
    }
    return FALSE;
}
SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
```

要点：
- handler 运行在**独立线程**上，只能做异步信号安全的操作（置 atomic、调 `server.stop()` 这种线程安全的）——不能做文件 I/O
- 返回 `TRUE` 阻止系统默认终止；返回 `FALSE` 交给下一个 handler
- `stop()` 让 `listen()` 返回 → serve() 正常 return → Recorder 析构 flush → 数据落盘

**这套机制 server.cpp 里已经装好了**（第 34–53 行）。run 命令要做的不是重装一遍，而是**复用**它——把 serve 改成可外部 stop 的形式。

## 7. sessionId 与目录

`stampNow()` 给形如 `20261118-193012` 的时间戳（见 clock.h）。但同一秒可能起两次 run → 目录冲突。原项目用 `makeSessionId()` 加随机后缀。minixray 简化：时间戳 + 毫秒 + 序号，或直接用 `stampNow()` 加递增计数。sessionId 只允许 `[A-Za-z0-9_-]`（finalize 的校验），时间戳格式天然满足。
