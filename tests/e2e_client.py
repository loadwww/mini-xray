"""e2e 子进程：通过代理发 3 个请求（非流式、流式、错误路径）。

由 mxr run 以子进程方式启动——此时 OPENAI_BASE_URL 已注入环境，
但脚本直接连代理端口，避免依赖客户端 SDK。
退出码 0 = 三个响应都符合预期；非 0 = 有一个不符。
端口由 MXR_PROXY_PORT 覆盖（默认 18080）。
"""
import json
import os
import sys
import urllib.error
import urllib.request

port = os.environ.get("MXR_PROXY_PORT", "18080")
base = "http://127.0.0.1:%s/v1/chat/completions" % port
S = {"role": "system", "content": "You are helpful."}


def post(body):
    req = urllib.request.Request(
        base,
        data=json.dumps(body).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


def check(label, cond, detail):
    print("  %s: %s" % (label, "ok" if cond else "FAIL " + detail))
    return cond


ok = True
m1 = [S, {"role": "user", "content": "e2e normal"}]
s1, b1 = post({"model": "mock-1", "messages": m1})
ok &= check("normal (200 + content)", s1 == 200 and "hello from mock" in b1,
            "status=%s body=%s" % (s1, b1[:100]))

m2 = m1 + [{"role": "assistant", "content": "hello from mock"},
           {"role": "user", "content": "e2e stream"}]
s2, b2 = post({"model": "mock-1", "stream": True, "messages": m2})
ok &= check("stream (200 + [DONE])", s2 == 200 and "[DONE]" in b2,
            "status=%s body_tail=%s" % (s2, b2[-100:]))

m3 = m2 + [{"role": "assistant", "content": "tok0 tok1"},
           {"role": "user", "content": "e2e error"}]
s3, b3 = post({"messages": m3})  # 故意不带 model → mock 上游返回 400
ok &= check("error path (400)", s3 == 400, "status=%s body=%s" % (s3, b3[:100]))

sys.exit(0 if ok else 1)
