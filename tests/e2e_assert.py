"""e2e 产物断言：在临时工作目录里运行，自动找最新的 runs/<session>/。

断言失败时打印具体字段的期望值与实际值，并以退出码 1 结束。
两个平台脚本共用这一个断言程序，避免逻辑分叉。
"""
import collections
import glob
import json
import os
import sys

# CI 的 Python stdout 在 Windows 上默认 cp1252，打印中文标签会 UnicodeEncodeError。
# 显式切到 UTF-8，让本地与 CI 行为一致（Python 3.7+ 支持 reconfigure）。
sys.stdout.reconfigure(encoding="utf-8")

failures = []


def check(cond, label, detail=""):
    print("  [%s] %s%s" % ("ok" if cond else "FAIL", label,
                           "" if cond else "  << " + detail))
    if not cond:
        failures.append(label)


dirs = sorted(glob.glob("runs/*/"), key=os.path.getmtime, reverse=True)
check(bool(dirs), "run 目录存在")
if not dirs:
    sys.exit(1)
d = dirs[0]
print("  产物目录: %s" % d)

for name in ("events.jsonl", "meta.json", "run.json", "graph.mmd", "timeline.mmd"):
    check(os.path.isfile(os.path.join(d, name)), "文件存在: " + name)

events = []
try:
    with open(os.path.join(d, "events.jsonl"), encoding="utf-8") as f:
        events = [json.loads(line) for line in f if line.strip()]
except Exception as e:
    check(False, "events.jsonl 可解析", str(e))

types = collections.Counter(e.get("type") for e in events)
check(types.get("req") == 3, "req 事件数 = 3", str(types.get("req")))
check(types.get("resp") == 3, "resp 事件数 = 3", str(types.get("resp")))
check(types.get("sse_end") == 1, "sse_end 事件数 = 1", str(types.get("sse_end")))
check(types.get("sse_chunk", 0) >= 10, "sse_chunk >= 10", str(types.get("sse_chunk")))
check(any(e.get("status") == 400 for e in events if e.get("type") == "resp"),
      "错误路径 400 已落盘")

try:
    with open(os.path.join(d, "meta.json"), encoding="utf-8") as f:
        meta = json.load(f)
    for key, want in (("run_count", 1), ("step_count", 3), ("total_tokens", 35),
                      ("parse_failed", 0), ("dropped_events", 0)):
        check(meta.get(key) == want, "meta.%s = %s" % (key, want),
              "got=%s" % meta.get(key))
except Exception as e:
    check(False, "meta.json 可解析", str(e))

for name, prefix in (("graph.mmd", "flowchart TD"), ("timeline.mmd", "gantt")):
    try:
        with open(os.path.join(d, name), encoding="utf-8") as f:
            text = f.read()
        check(text.lstrip().startswith(prefix), "%s 以 %s 开头" % (name, prefix),
              repr(text[:40]))
    except Exception as e:
        check(False, "%s 可读" % name, str(e))

if failures:
    print("\n%d 条断言失败: %s" % (len(failures), ", ".join(failures)))
    sys.exit(1)
print("\n所有产物断言通过")
