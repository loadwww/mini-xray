#Requires -Version 5.1
# T8.3 e2e（Windows / PowerShell 5.1）：起 mock 上游 → mxr run 发 3 个请求 → 断言落盘 → 清理进程。
# 可用环境变量覆盖：MXR_BUILD_DIR / MXR_PROXY_PORT / MXR_UPSTREAM_PORT / MXR_PYTHON / MXR_BIN / MOCK_BIN
#
# 两个 Windows 专属坑（已在脚本中处理）：
#   1. 本文件必须是 UTF-8 with BOM，否则 5.1 按 cp1252 解析中文注释会炸；
#   2. 生成的 config.json 必须无 BOM，否则 nlohmann::parse 会把 BOM 当非法首字符——
#      所以不能用 Set-Content -Encoding UTF8（它会写 BOM），要用 WriteAllText + UTF8Encoding($false)。

# 原生程序的 stderr 在 5.1 会触发 NativeCommandError，这里关闭并用 $LASTEXITCODE 判断成败
$ErrorActionPreference = 'Continue'

function Fail { param([string]$msg) Write-Host "[e2e FAIL] $msg" -ForegroundColor Red; exit 1 }
function Step { param([string]$msg) Write-Host "[e2e] $msg" }

function Test-Port {
    param([int]$port)
    $client = New-Object System.Net.Sockets.TcpClient
    try { $client.Connect('127.0.0.1', $port); return $true }
    catch { return $false }
    finally { $client.Close() }
}

$root = Split-Path -Parent $PSScriptRoot
$buildDir = if ($env:MXR_BUILD_DIR) { $env:MXR_BUILD_DIR } else { Join-Path $root 'build' }
$proxyPort = if ($env:MXR_PROXY_PORT) { [int]$env:MXR_PROXY_PORT } else { 18080 }
$upstreamPort = if ($env:MXR_UPSTREAM_PORT) { [int]$env:MXR_UPSTREAM_PORT } else { 19100 }
$python = if ($env:MXR_PYTHON) { $env:MXR_PYTHON } else { 'python' }
$mxrBin = if ($env:MXR_BIN) { $env:MXR_BIN } else { Join-Path $buildDir 'mxr.exe' }
$mockBin = if ($env:MOCK_BIN) { $env:MOCK_BIN } else { Join-Path $buildDir 'mock_upstream.exe' }
$work = Join-Path $buildDir 'e2e-work'

# ── 0. 前置检查 ────────────────────────────────────────────────────
if (-not (Test-Path $mxrBin -PathType Leaf)) { Fail "找不到 mxr：$mxrBin（先编译）" }
if (-not (Test-Path $mockBin -PathType Leaf)) { Fail "找不到 mock_upstream：$mockBin（先编译）" }
if (Test-Port $proxyPort) { Fail "代理端口 $proxyPort 已被占用（残留进程？）" }
if (Test-Port $upstreamPort) { Fail "上游端口 $upstreamPort 已被占用（残留进程？）" }

if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Path $work -Force | Out-Null
Copy-Item (Join-Path $PSScriptRoot 'e2e_client.py') $work
Copy-Item (Join-Path $PSScriptRoot 'e2e_assert.py') $work

# 关键：无 BOM 的 UTF-8（见文件头注释第 2 条）
$config = @"
{
  "port": $proxyPort,
  "root": ".",
  "proxy": {
    "injectUsage": true,
    "upstreams": [
      { "match": "/v1", "url": "http://127.0.0.1:$upstreamPort/v1", "name": "mock" }
    ]
  }
}
"@
[System.IO.File]::WriteAllText(
    (Join-Path $work 'config.json'), $config,
    (New-Object System.Text.UTF8Encoding($false)))

# ── 1. 起 mock 上游（finally 保证任何退出路径都清理） ───────────────
Step "起 mock 上游 127.0.0.1:$upstreamPort"
# 不用 Start-Process：5.1 在环境变量同时含 https_proxy / HTTPS_PROXY 时会抛
# "Item has already been added"（它把环境变量塞进大小写不敏感的字典）。
# ProcessStartInfo 直接起进程，没有这个缺陷。
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $mockBin
$psi.Arguments = "$upstreamPort"
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$mockProc = [System.Diagnostics.Process]::Start($psi)

try {
    $ready = $false
    for ($i = 0; $i -lt 50; $i++) {
        if (Test-Port $upstreamPort) { $ready = $true; break }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) { Fail "mock 上游未在 5 秒内就绪（见上方 mock 输出）" }

    # ── 2. mxr run ──────────────────────────────────────────────────
    Step "mxr run：3 个请求（非流式 / 流式 / 错误路径）"
    Push-Location $work
    try {
        $env:MXR_PROXY_PORT = "$proxyPort"
        & $mxrBin run -- $python 'e2e_client.py'
        if ($LASTEXITCODE -ne 0) { Fail "mxr run 失败（rc=$LASTEXITCODE）" }

        # ── 3. 断言产物 ─────────────────────────────────────────────
        Step "断言 events.jsonl / meta.json / 图产物"
        & $python 'e2e_assert.py'
        if ($LASTEXITCODE -ne 0) { Fail "产物断言失败（rc=$LASTEXITCODE）" }
    }
    finally { Pop-Location }
}
finally {
    if ($mockProc -and -not $mockProc.HasExited) {
        Stop-Process -Id $mockProc.Id -Force -ErrorAction SilentlyContinue
    }
}

# ── 4. 确认端口释放 ────────────────────────────────────────────────
for ($i = 0; $i -lt 20; $i++) {
    if (-not (Test-Port $upstreamPort) -and -not (Test-Port $proxyPort)) { break }
    Start-Sleep -Milliseconds 100
}
if (Test-Port $upstreamPort) { Fail "mock 端口 $upstreamPort 未释放" }
if (Test-Port $proxyPort) { Fail "代理端口 $proxyPort 未释放" }

Step "PASS — 3 个请求已落盘，进程与端口已清理（工作目录保留在 $work 供排查）"
