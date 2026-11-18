# Web UI 服务端路由原型速查（T7.1）

只放 API 原型与判据，不放实现。

## cpp-httplib 路由与参数

```cpp
httplib::Server svr;
svr.Get("/__xray", [](const httplib::Request &req, httplib::Response &res) {
    // 处理
});
svr.Get(R"(/.*)", handler);   // 通配兜底

// 请求侧
bool has = req.has_param("dir");                    // 有没有这个 query 参数
std::string v = req.get_param_value("dir");          // 取第一个值（自动 URL 解码）

// 响应侧
res.status = 200;                                   // 或 httplib::StatusCode::OK_200
res.set_content("...", "text/html; charset=utf-8");
res.set_content(json.dump(), "application/json; charset=utf-8");
```

## std::filesystem 安全路径判据

```cpp
namespace fs = std::filesystem;
fs::path root = fs::weakly_canonical(fs::absolute(rootDir));   // 解析 .. 与符号链接
fs::path candidate = fs::weakly_canonical(root / userInput);
bool inside = candidate.string().rfind(root.string(), 0) == 0; // 前缀判断
```

**弱规范化的意义**：`root / "../../etc"` 拼出来字符串看着在 root 下，
`weakly_canonical` 会把 `..` 实际解析掉，露出 `/etc`——再判前缀就拒绝了。
只做字符串 `find("..")` 挡不住 URL 编码（`%2e%2e`）、反斜杠变体、符号链接。

边界判据（自己写防护时按这个来）：
1. 拒绝空串、绝对路径（`/` 或 `C:` 开头）
2. 拒绝任何含 `..` 路径段的输入（标准化后判）
3. 只允许 root 的**直接子目录**：解析后的父目录必须恰好是 root
   （不然 `dir=a/b` 能读到 root 深处的目录，虽然没越狱但超出了约定）
4. 用 `weakly_canonical` 而非纯字符串操作

## 轮询为什么够用（3 秒）

- SSE / WebSocket 需要服务端主动推送，要维护长连接状态、心跳、断线重连
- 本 UI 是**只读监控**：数据变化频率低（一次 agent 任务几秒到几十秒），
  3 秒轮询的延迟用户无感，代码却简单一个数量级
- 判据：**数据更新频率 × 用户对延迟的容忍度** 决定要不要推送。
  监控面板 3 秒轮询足够；聊天界面必须推送。

## HTML 内嵌为字符串常量

```cpp
static const char *kHtml = R"HTML(
<!doctype html>
<html>...</html>
)HTML";
```

原始字符串字面量 `R"HTML(...)HTML"`：中间所有引号、反斜杠、换行都不需要转义。
分隔符 `HTML` 不能出现在内容里，否则提前闭合。

## 前端边界（工作单声明）

- 只调 `/__xray/api/*`，不假设文件路径、不读本地文件
- 不引入构建工具（无 React/Vite/npm），CDN 只白名单 mermaid.js
- `setInterval` 3 秒只刷新数据区，不整页 reload

## 判据小结

- 路径穿越防护是**安全判断**，必须用户手写（AI 代劳失去练习意义）
- 前端 HTML/JS 是**打字速度**，AI 代写 + 用户审 diff（无外链非白名单 CDN、无路径拼接进 DOM）
