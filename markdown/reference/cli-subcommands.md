# CLI 子命令原型速查（T7.2）

## 1. 退出码约定（脚本靠它判断成败）

| 值 | 含义 | 什么时候用 |
|---|---|---|
| `0` | 成功 | 命令完成（哪怕伴随 warning，如配置降级） |
| `1` | 运行时失败 | 要做的事没做成：文件读不了、格式错、目标不存在 |
| `2` | 用法错误 | 用户敲错了：缺参数、未知选项、多余参数 |

CLI 是有契约的接口。退出码混用会让 `&&` / `if` 脚本静默走错分支。**用法错 ≠ 运行时失败**：缺参数是 2，路径存在但格式坏是 1。

现有 `cli.cpp` 已有三个常量：`kExitOk=0` / `kExitFail=1` / `kExitUsage=2`，直接用。

## 2. argv 解析原型

`int mxr::run(int argc, char* argv[])` 收到的原始参数：
- `argc` = 参数个数（含程序名，`argv[0]`）
- `argv[i]` 是 `char*`，比较前转 `std::string`

```cpp
const std::string cmd = argv[i];              // char* → string
if (cmd == "list") { ... }
if (i + 1 >= argc) { /* 缺参数 */ }           // 越界守卫
std::string val = argv[i + 1];
```

**全局 flag 只在子命令之前**：现有循环 `while (i < argc && argv[i][0] == '-')` 摘 `--verbose` / `--root`，遇到第一个非 `-` 开头就停，那个就是子命令。子命令自己的 flag（如 `--json`）由该命令的分支自己解析。

## 3. 目录扫描原型（list 用）

```cpp
#include <filesystem>
namespace fs = std::filesystem;

std::error_code ec;
for (const auto &entry : fs::directory_iterator(rootDir, ec)) {
    if (entry.is_directory(ec) && !ec) {
        std::string name = entry.path().filename().string();
        // 读 <name>/meta.json ...
    }
}
```

**必须用 error_code 重载**：rootDir 不存在时返回空列表，不让异常穿出。T7.1 的 `registerWebUi` 里已经用过同样的写法。

## 4. JSON 安全读取（list/meta、show/run 用）

```cpp
auto j = json::parse(text, nullptr, false);   // 坏数据 → discarded，不抛
if (!j.is_discarded() && j.is_object()) {
    int steps = j.value("step_count", 0);
    std::string id = j.value("id", std::string());
}
```

`value<T>(key, 默认值)` 的类型由默认值推导——默认值写 `0` 得到 int，写 `0LL` 得到 long long，写 `""` 得到 string。**字段实际类型必须和默认值一致**，否则抛 `type_error.302`。

## 5. 按步序号对齐（diff 的核心判据）

`diff` 对齐**不能用时间戳**——两次运行的绝对时间必然不同，且并行工具的 startMs 会重叠。正确做法是按语义位置：

```
base.steps[i]  ↔  cur.steps[i]     // 按 vector 下标（= step.index）
按 i 遍历：
  i >= base.size()          → cur 多出来的步 → "新增"
  i >= cur.size()           → base 多出来的步 → "删除"
  两者都有 → 逐字段比：model / usage.total_tokens / tools[].argsJson
```

**分叉点 = 第一个出现差异的 i**。报告里点名"第几步开始分叉"，就是第一个 `fields` 非空的 i。

工具callId 是跨 run 的唯一关联键，但两次运行的 id 不同（模型每次生成新 id）——所以 diff 里工具按**同一步内的下标**对齐，不按 id。

## 6. ANSI 颜色（show 可选）

```cpp
constexpr const char *RED = "\033[31m";
constexpr const char *RESET = "\033[0m";
std::cout << RED << "失败" << RESET << "\n";
```

注意：管道重定向时颜色码会污染输出（jq 解析失败）。约定：检测到 stdout 不是终端时不输出颜色，或提供 `--no-color`。简单实现可直接不用颜色。

## 7. list --json 的可解析性

输出的每行必须是合法 JSON，且**只有 JSON**——不能混日志文字。验证台用 `json::parse(body)` 断言。人类可读表格走默认，机器读走 `--json`，两条路径互斥。
