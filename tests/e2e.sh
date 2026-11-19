#!/usr/bin/env bash
# T8.3 e2e：起 mock 上游 → mxr run 发 3 个请求 → 断言落盘 → 清理进程。
# 可用环境变量覆盖：MXR_BUILD_DIR / MXR_PROXY_PORT / MXR_UPSTREAM_PORT / MXR_PYTHON / MXR_BIN / MOCK_BIN

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${MXR_BUILD_DIR:-$ROOT/build}"
PROXY_PORT="${MXR_PROXY_PORT:-18080}"
UPSTREAM_PORT="${MXR_UPSTREAM_PORT:-19100}"
PYTHON="${MXR_PYTHON:-python}"
# Windows 二进制带 .exe，Linux 不带——按平台选默认名（仍可用环境变量覆盖）
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) EXE_SUFFIX=".exe" ;;
  *) EXE_SUFFIX="" ;;
esac
MXR_BIN="${MXR_BIN:-$BUILD_DIR/mxr$EXE_SUFFIX}"
MOCK_BIN="${MOCK_BIN:-$BUILD_DIR/mock_upstream$EXE_SUFFIX}"
WORK="$BUILD_DIR/e2e-work"

fail() { echo "[e2e FAIL] $*" >&2; exit 1; }
step() { echo "[e2e] $*"; }

port_busy() {
    "$PYTHON" -c "import socket,sys; s=socket.socket(); s.settimeout(0.3); sys.exit(0 if s.connect_ex(('127.0.0.1',$1))==0 else 1)" "$1"
}

# ── 0. 前置检查 ────────────────────────────────────────────────────
[[ -f "$MXR_BIN" ]] || fail "找不到 mxr：$MXR_BIN（先编译）"
[[ -f "$MOCK_BIN" ]] || fail "找不到 mock_upstream：$MOCK_BIN（先编译）"
command -v "$PYTHON" >/dev/null 2>&1 || fail "找不到 python：$PYTHON"
if port_busy "$PROXY_PORT"; then fail "代理端口 $PROXY_PORT 已被占用（残留进程？）"; fi
if port_busy "$UPSTREAM_PORT"; then fail "上游端口 $UPSTREAM_PORT 已被占用（残留进程？）"; fi

rm -rf "$WORK"
mkdir -p "$WORK"
cp "$ROOT/tests/e2e_client.py" "$ROOT/tests/e2e_assert.py" "$WORK/"

cat > "$WORK/config.json" <<EOF
{
  "port": $PROXY_PORT,
  "root": ".",
  "proxy": {
    "injectUsage": true,
    "upstreams": [
      { "match": "/v1", "url": "http://127.0.0.1:$UPSTREAM_PORT/v1", "name": "mock" }
    ]
  }
}
EOF

# ── 1. 起 mock 上游（trap 保证任何退出路径都清理） ──────────────────
step "起 mock 上游 127.0.0.1:$UPSTREAM_PORT"
"$MOCK_BIN" "$UPSTREAM_PORT" > "$WORK/mock.log" 2>&1 &
MOCK_PID=$!
SERVE_PID=""
cleanup() {
    if kill -0 "$MOCK_PID" 2>/dev/null; then
        kill "$MOCK_PID" 2>/dev/null || true
        wait "$MOCK_PID" 2>/dev/null || true
    fi
    if [[ -n "$SERVE_PID" ]] && kill -0 "$SERVE_PID" 2>/dev/null; then
        kill -TERM "$SERVE_PID" 2>/dev/null || true
        wait "$SERVE_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

for _ in $(seq 1 50); do
    if port_busy "$UPSTREAM_PORT"; then break; fi
    sleep 0.1
done
port_busy "$UPSTREAM_PORT" || fail "mock 上游未在 5 秒内就绪（见 $WORK/mock.log）"

# ── 2. 发 3 个请求 ──────────────────────────────────────────────────
# Windows：mxr run 一条龙（代理 + 子进程 + 收尾 finalize），与 T8.3 一致。
# Linux：run 依赖 CreateProcessW 未移植，改用 serve（后台）+ 客户端直发 +
#        SIGTERM 优雅停服（Recorder flush）+ mxr finish 重建报告包。
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    step "mxr run：3 个请求（非流式 / 流式 / 错误路径）"
    (cd "$WORK" && MXR_PROXY_PORT="$PROXY_PORT" "$MXR_BIN" run -- "$PYTHON" e2e_client.py) \
        || fail "mxr run 失败（子进程断言或落盘出错，见上方输出）"
    ;;
  *)
    step "serve + 客户端：3 个请求（Linux：run 未移植，走 serve + finish）"
    (cd "$WORK" && "$MXR_BIN" serve config.json > "$WORK/serve.log" 2>&1) &
    SERVE_PID=$!
    for _ in $(seq 1 50); do
        if port_busy "$PROXY_PORT"; then break; fi
        sleep 0.1
    done
    port_busy "$PROXY_PORT" || fail "代理未在 5 秒内就绪（见 $WORK/serve.log）"
    (cd "$WORK" && MXR_PROXY_PORT="$PROXY_PORT" "$PYTHON" e2e_client.py) \
        || fail "客户端 3 个请求断言失败"
    # SIGTERM → POSIX handler 置标志 → stopWatcher 调 svr.stop() → listen 返回 → Recorder flush
    kill -TERM "$SERVE_PID"
    wait "$SERVE_PID" || true
    SESSION=$(ls -1 "$WORK/runs" 2>/dev/null | head -1)
    [[ -n "$SESSION" ]] || fail "serve 未生成 run 目录"
    step "mxr finish $SESSION：从 events.jsonl 重建报告包"
    (cd "$WORK" && "$MXR_BIN" finish "$SESSION") || fail "mxr finish 失败"
    ;;
esac

# ── 3. 断言产物 ─────────────────────────────────────────────────────
step "断言 events.jsonl / meta.json / 图产物"
(cd "$WORK" && "$PYTHON" e2e_assert.py) || fail "产物断言失败"

# ── 4. 清理并确认端口释放 ──────────────────────────────────────────
cleanup
trap - EXIT
for _ in $(seq 1 20); do
    if ! port_busy "$UPSTREAM_PORT" && ! port_busy "$PROXY_PORT"; then break; fi
    sleep 0.1
done
port_busy "$UPSTREAM_PORT" && fail "mock 端口 $UPSTREAM_PORT 未释放"
port_busy "$PROXY_PORT" && fail "代理端口 $PROXY_PORT 未释放"

step "PASS — 3 个请求已落盘，进程与端口已清理（工作目录保留在 $WORK 供排查）"
