# CI 双平台原型速查（T8.4）

> 这里只列 API / 语法原型与判据，不给完整实现。CI 是本关核心判断，由你手写。

## 1. Workflow 骨架（GitHub Actions）

文件位置固定：`.github/workflows/ci.yml`（目录不存在就新建）。

```yaml
name: CI
on: [push, pull_request]          # 触发时机：每次推送与 PR

jobs:
  windows:
    runs-on: windows-latest       # GitHub 托管 Windows 镜像
    steps:
      - uses: actions/checkout@v7 # 拉代码（2026-11 当前主版本）
      - name: 某一步
        run: echo hello           # 每条 run 是一个独立 shell
  linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v7
```

**判据**：`run` 步骤的**退出码**决定 job 成败——非 0 即红。这与 T8.1 selftest 的契约完全一致；CI 不解析日志里的 "PASS"。

## 2. 版本号（2026-11 核实）

| Action | 当前主版本 | 说明 |
|---|---|---|
| `actions/checkout` | `@v7` | Node 24 运行时 |
| `actions/setup-python` | `@v7` | 用于 e2e 客户端 |
| `ilammy/msvc-dev-cmd` | `@v1` | 暂无 Node 24 新版；Windows 初始化 MSVC x64 环境用 |

替代方案：不用 `msvc-dev-cmd`，直接在 `run` 里调 `vswhere` + `vcvarsall.bat`，但更繁琐。

## 3. Windows：初始化 MSVC 环境

```yaml
- uses: ilammy/msvc-dev-cmd@v1
  with:
    arch: x64
- run: cmake -S . -B build
- run: cmake --build build --config Release
```

**为什么需要这一步**：本机靠 `dev-shell.cmd` / 手动 `export INCLUDE/LIB`；CI 是全新镜像，没有这些环境变量，cmake 找不到 MSVC 编译器。

## 4. Linux：编译器与依赖

```yaml
- run: sudo apt-get update && sudo apt-get install -y cmake ninja-build
```

**注意**：当前 CMakeLists 无条件 `target_link_libraries(... ws2_32)`，Linux 链接器不认识这个库——必须先做平台条件化：

```cmake
if(WIN32)
    target_link_libraries(mxr PRIVATE ws2_32)
    # ... 其它目标同理
endif()
```

这是本关必须解决的第一个跨平台障碍，属于你的判断范围。

## 5. 平台相关的源码障碍（第二个障碍）

`src/proxy/spawn.cpp` 当前结构：

- 辅助函数 `toWide` / `quoteArg` / `buildEnvBlock` 在 `#ifdef _WIN32` 内；
- `spawnWithEnv` 函数体直接调用 `CreateProcessW` / `WaitForSingleObject` / `GetExitCodeProcess`；
- **Linux 下这些标识符都不存在** → 编译失败。

可选路线（你自己权衡）：

| 路线 | 做法 | 代价 |
|---|---|---|
| A 完整移植 | Linux 分支用 `fork` + `execvp` + `waitpid` + `setenv`，实现同语义 | 工作量大，但真正双平台 |
| B 降级跳过 | CMake 里 Linux 不编译 spawn/runner，e2e 改用直接 `serve` + 客户端脚本 | 牺牲 `mxr run` 在 Linux 的覆盖 |
| C CI 只跑 Windows | Linux job 仅 build 核心库 + selftest 子集 | 偏离任务书双平台 e2e 目标 |

任务书要求"Linux 也跑 e2e"，路线 C 不满足；路线 B 要改 e2e 脚本。选择前先想清楚"CI 到底要守住什么"。

## 6. 换行符坑（第三个障碍）

本机 `git config core.autocrlf = true`，且仓库**没有 `.gitattributes`**。后果：`tests/e2e.sh` 以 LF 入库，Linux checkout 时被转成 CRLF → `bash: ./e2e.sh: /bin/bash^M: bad interpreter`。

```gitattributes
# 新建仓库根目录 .gitattributes
tests/e2e.sh text eol=lf
tests/*.py text eol=lf
```

**判据**：`.sh` 和 `.py` 必须 eol=lf；`.ps1` 必须 CRLF + BOM（T8.3 已处理 BOM，但 autocrlf 可能改换行——需要一并指定 `*.ps1 text eol=crlf`）。

## 7. 验证 CI 真的在跑

任务书验收第 2 条："故意提交一个编译错误 → CI 红"。做法：

1. 在源码里改一处明显的编译错误（如删掉一个分号），push；
2. 观察两个 job 都在 build 步骤红；
3. 立刻 revert 再 push，确认恢复绿。

**不要**在 main 上长期留着红提交；用一个临时分支或紧跟一个修复提交。

## 8. 远端前提

当前仓库**无 remote**（`git remote -v` 为空）。CI 要跑起来必须先：

```bash
git remote add origin <你的仓库URL>
git push -u origin master
```

仓库可见性、分支保护、Actions 权限（Settings → Actions → General → Workflow permissions）都要你确认。
