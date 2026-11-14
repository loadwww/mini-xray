# mini_xray 学习笔记

AgentXray 的重写版学习项目。目标：自己从零写出一个能抓 AI Agent 流量的反向代理。
本文件记录「怎么构建」和「踩过的坑」，目的是**换一台干净机器，只凭这一份文件就能重建**。

**文档地图**（所有 Markdown 都在 `markdown/` 下，按用途分子目录）：

```
markdown/
├─ NOTES.md        本文件：环境、构建命令、踩过的坑
├─ notes/          每关的口述题答案（概念、判据），隔一段时间复习用
└─ reference/      标准库/第三方 API 的函数原型速查（按主题，不按任务），写代码前查签名
```

`markdown/` 同时是一个 Obsidian vault（`.obsidian/` 是它的配置，不进 git）。

---

## 一、环境前置

| 组件 | 本机版本 | 说明 |
|---|---|---|
| Visual Studio 2022 Community | MSVC 19.41.34123（工具集 14.41.34120） | 提供 cl / link 与 Windows SDK 绑定 |
| CMake | 3.29.5-msvc4 | VS 自带，**不在系统 PATH** |
| Ninja | 1.11.0 | VS 自带，**不在系统 PATH** |
| Windows SDK | 10.0.22621.0 | 本机装在 `D:\Windows Kits\10`（不在 C 盘） |

本机路径：

```
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe
```

### 为什么用 Ninja，而不是 Visual Studio 生成器

本机 VS 的**安装实例注册丢失**了：`vcvars64.bat` 和 `cl` 都正常，但

```
"C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe" repair --installPath ...
→ Warning: 找不到与以下参数匹配的已安装产品
```

VS Installer 自己都不认为这份 VS 装过，`repair` 无从下手。而 CMake 的
`Visual Studio 17 2022` 生成器**只认安装器登记的实例**，不扫硬盘，于是报：

```
Generator Visual Studio 17 2022 could not find any instance of Visual Studio.
```

换成 **Ninja** 生成器即绕过：它不做实例检测，只要 `cl.exe` 在 PATH 里就能用。
代价是不能双击 `.sln` 用 IDE 打开工程——本项目全程命令行，无影响。

**注意**：这是本机特有环境。换机器时如果 VS 安装完好，VS 生成器也能用，
但构建命令要改（见下）。

### 构建必须在什么终端里跑

两种都行：

1. 开始菜单 → `x64 Native Tools Command Prompt for VS 2022`
2. 普通 cmd 里先执行：

```
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
```

`vcvars64.bat` 只修改**当前这个终端进程**的环境变量（PATH / INCLUDE / LIB），
不影响系统全局，也不影响已经开着的其他窗口。

---

## 二、构建命令

```
cmake -S . -B build -G Ninja
cmake --build build
build\mxr.exe --version        :: cmd
.\build\mxr.exe --version      :: PowerShell
```

- `-S .` = 源码目录（Source）是当前目录
- `-B build` = 构建目录（Build），**大写 B**
- `-G Ninja` = 生成器。**不需要 `-A x64`**（那是 VS 生成器的参数，架构由终端环境决定）
- Ninja 是**单配置**生成器，构建阶段**没有** `--config`；需要 Release 就在配置时加
  `-DCMAKE_BUILD_TYPE=Release`（默认空，等价于无优化 + 带调试信息）
- 产物在 `build\mxr.exe`，**不在** `build\Release\` 下

VS 生成器的对应写法（仅在实例注册完好的机器上可用）：

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release      :: 配置在构建阶段选，产物在 build\Release\
```

### 清理重建

```
rmdir /s /q build                    :: cmd
Remove-Item -Recurse -Force build    :: PowerShell
```

`CMakeCache.txt` 会把上次的生成器和编译器配置锁死在里面。换了生成器或改了工具链
必须先删 `build/`，否则报 "generator does not match the generator used previously"。

---

## 三、目录结构

```
mini_xray/
├─ CMakeLists.txt        构建描述（mxr / route_test / recorder_test / 各验证台）
├─ config.json           运行期配置样例（proxy.upstreams 按**路径前缀**匹配）
├─ dev-shell.cmd         一键进入 vcvars 环境的 cmd
├─ markdown/             所有文档（见上方文档地图）
├─ build/                构建目录，不进 git，可随时删掉重建
├─ runs/                 每次 serve 落一个 <YYYYMMDD-HHMMSS>/events.jsonl，不进 git
├─ src/
│  ├─ main.cpp           只有一行 return mxr::run(argc, argv);
│  ├─ cli/               argv 解析、子命令分派（cfg / echo / serve / version）
│  ├─ config/            配置加载（nlohmann/json）与序列化
│  ├─ common/            version / FileWriter / clock（nowMs、stampNow）
│  ├─ store/             JsonlWriter、Event、redact（脱敏）、Recorder（异步写线程）
│  ├─ http/              HttpRequest + readRequest（手写报文解析）
│  ├─ net/               WsaGuard / Socket / Listener（winsock RAII）
│  └─ proxy/             route（路径段匹配）、target（URL 拆分）、server（httplib 转发）
├─ tests/                验证台，每个都是独立可执行文件
└─ third_party/          nlohmann/json.hpp、cpp-httplib/httplib.h
```

---

## 四、踩坑记录

### 1. `cl hello.cpp` 的产物不落在源文件旁边

**现象**：`cl /EHsc D:\mini_xray\hello.cpp`，`hello.obj` / `hello.exe` 却出现在
`C:\Users\欧阳晨\`，`D:\mini_xray` 里什么都没有。

**原因**：`cl` 的输出位置由**当前工作目录（CWD）**决定。源文件路径只告诉它读哪个文件，
不决定写到哪里。

**验证**：`/Fe:路径\名字.exe` 指定 exe 落点，`/Fo:路径\` 指定 obj 落点。

### 2. `/EHsc` 不是性能开关，是正确性开关

**现象**：抛异常穿过一个带析构函数的作用域时，不加 `/EHsc` 的版本析构函数**不执行**，
并报 `warning C4530`。

**原因**：`/EH` 让编译器为每个可能抛异常的函数生成**栈展开信息**（unwind tables）。
没有这张表，异常从 `throw` 跳到 `catch` 的过程中，中间跨过的栈帧被直接丢弃，
里面的局部对象析构函数一律不调用 → 句柄、锁、缓冲全部泄漏。

`/EHsc` 里的 `s` 表示「假定 `extern "C"` 函数不抛异常」，正因如此调用 C 函数时
不必生成展开簿记，代码反而更小。

**验证**：写一个析构时打印的 `struct Guard`，在 `f()` 内部构造后 `throw 1;`，
`try/catch` 放在 `main` 里。两次编译对比 `/Fe:noeh.exe` 与 `/Fe:eh.exe`。

**注意**：`/EHsc` **不阻止**程序终止。异常没人捕获照样 `terminate()`。

### 3. `#include "x.h"` 的搜索起点是「包含它的那个文件」所在的目录

**现象**：`src/common/version.cpp` 里 `#include "version.h"` 编译通过，
`src/main.cpp` 里同一行报 `fatal error C1083: 无法打开包括文件: "version.h"`。

**原因**：

- `#include "x.h"` → 先搜**当前文件所在目录**，再搜 include 路径
- `#include <x.h>` → 跳过第一步，只搜 include 路径

`main.cpp` 在 `src/` 下，`src/version.h` 不存在，include 路径里也没有，于是失败。

**修法**：`#include "common/version.h"`（相对路径），或者
`target_include_directories(mxr PRIVATE src/common)` + `#include "version.h"`。

后者更好：加目录只需一处配置，源文件怎么搬家都不用改 include 写法。

### 4. CMake 的 VS 生成器找不到已装的 VS

见上文「为什么用 Ninja」。诊断命令（一条都不打印 → 实例注册确实没了）：

```
"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -all -products * -format value -property installationPath
```

### 5. 构建失败要先看它卡在哪一步

`[1/3]` `[2/3]` 都成功、`[3/3] Linking` 失败 → **不是编译错误，是链接错误**。

- `LNK2019 无法解析的外部符号` = 有人用了这个函数，没人提供定义
- `LNK2005 已经在 xxx.obj 中定义` = 同一个符号被提供了两份
- `LNK1120` / `LNK1169` 只是收尾的计数与结论，真正原因在上面那条 LNK20xx

编译阶段只查「这个函数**存在吗**」（有声明就够），链接阶段才查「这个函数**在哪**」（要定义）。

### 6. `#pragma once` 管不到跨编译单元

**现象**：把 `versionString()` 的定义写进 `version.h`、去掉 `inline` 后，
`main.cpp` 和 `version.cpp` 各自正常编译，链接时报
`LNK2005 ... 已经在 main.cpp.obj 中定义`。

**原因**：`#pragma once` 的作用域是**一个编译单元**，防的是同一个 `.cpp` 里重复
`#include` 同一个头文件。而这里两个 `.cpp` **各展开了一次**，各自都没违规；
问题在于每个编译单元各生成了一份函数定义。

**规则（ODR）**：非 inline 的普通函数，整个程序中只能有**一处**定义。
`inline` 允许出现多份完全相同的定义，由**链接器**保留一份、丢弃其余。
（不是「编译阶段合并」——编译单元互相看不见，没法合并。）

### 7. cmd 里 `%errorlevel%` 是解析时展开

**现象**：`build\mxr.exe & echo 退出码=%errorlevel%` 打印 0，但程序明明返回了非 0。

**原因**：cmd 处理一整行时，先把所有 `%VAR%` 替换成当前值，再执行。
所以拿到的是**上一条命令**的退出码。

**正确姿势**：分行写；或用 `if errorlevel 1`；或用 `build\mxr.exe || echo 失败`。

**顺带**：`return -1` 在 cmd 里显示 `-1`，但进程退出码是 32 位无符号，别处看到的是
`4294967295`。本项目约定：0 成功、1 运行时错误、2 用法错误。

### 8. 终端里误触 `>`，造出游离文件并提交

**现象**：仓库根先后出现 `git`、`master)`、`tatus` 三个空/垃圾文件，且被
`git add -A` 带进了提交。

**原因**：把终端输出粘回命令行，或手滑输入 `>`，cmd 把它后面的半个词当成重定向文件名。

**对策**：`git add -A` 之前**必须**先看 `git status`，逐行读完 `??` 列表。
不认识的文件名 = 立刻停下。
