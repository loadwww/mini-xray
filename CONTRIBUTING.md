# 参与开发

## 环境

- **Windows**：Visual Studio 2022（MSVC x64）、CMake ≥ 3.20、Ninja、Python 3。CMake/Ninja 随 VS 安装，但不在系统 PATH；`scripts/build.ps1` 会自动补。
- **Linux**：`cmake`、`ninja-build`、`python3`（CI 用 Ubuntu 22.04）。
- 第三方库 `cpp-httplib` 与 `nlohmann/json` 是 header-only，随仓库放在 `third_party/`，不需要额外安装。

## 构建

Windows（推荐一键脚本；会跑 selftest 与 e2e）：

```bat
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

或者手动（在 x64 Native Tools Command Prompt 里）：

```bat
cmake -S . -B build -G Ninja
cmake --build build
```

Linux：

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

`build/` 被 git 忽略；Ninja 是单配置生成器，构建阶段不要加 `--config`。

## 测试

| 命令 | 覆盖 | 耗时 |
|---|---|---|
| `mxr selftest` | 18 台（Win）/ 17 台（Linux）验证台，含 fixture 回归 78 条断言 | 数秒 |
| `mxr selftest --list` | 列出验证台 | 瞬时 |
| `mxr selftest sse` | 只跑一台（支持简名） | 亚秒级 |
| `tests/e2e.ps1` / `bash tests/e2e.sh` | 真起 mock 上游和代理、发 3 个请求、断言产物、清理端口 | 数秒 |

CI 在 `windows-latest` 与 `ubuntu-latest` 跑同样的构建、selftest 和 e2e，只看退出码。
本地改动涉及解析、落盘、CLI 或 CMake 时，至少把 selftest 和对应平台的 e2e 跑一遍。

## 提交前检查

1. 代码能编译，无新增警告就别引入。
2. `mxr selftest` 全绿；改了核心函数就确认 fixture 仍能抓到回归。
3. 涉及新 API 时，在 `markdown/reference/` 补一份原型速查。
4. 教学脚手架（填空编号、分步提示、输入→输出样例）不进提交；验收通过后从源码里清掉，只保留功能说明。
5. 文档里描述的行为要和代码一致；不支持的能力写进 README 的 Limitations，不要含糊。

## 平台边界

`src/net/`、`src/http/`、`src/proxy/spawn.cpp`、`src/proxy/runner.cpp` 只在 Windows 编译。
Linux 用 `serve` + `finish` 代替 `run`。新增源码若依赖平台 API，请在 CMakeLists.txt 里显式条件化，并同步更新 README 支持矩阵。

## License

提交即表示你同意贡献内容以 MIT 协议发布。
