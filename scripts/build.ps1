# One-shot build + test for mini_xray on Windows.
# Usage (from x64 Native Tools prompt, or just from PowerShell):
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -SkipE2E

[CmdletBinding()]
param(
    [string]$BuildDir = "build",
    [switch]$SkipE2E
)

# Native commands write to stderr for normal diagnostics; with 'Stop' PowerShell 5.1
# turns that into NativeCommandError. We check $LASTEXITCODE ourselves instead.
$ErrorActionPreference = 'Continue'

function Invoke-Native {
    param([string]$Exe, [string[]]$CmdArgs)
    Write-Host "==> $Exe $($CmdArgs -join ' ')"
    & $Exe @CmdArgs
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe exited with code $LASTEXITCODE"
    }
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Push-Location $repoRoot
try {
    # 1. Make sure the MSVC x64 environment is present.
    if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
        $vsRoot = ${env:ProgramFiles(x86)}
        $vcvars = Join-Path $vsRoot 'Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
        if (-not (Test-Path $vcvars)) {
            $vcvars = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
        }
        if (-not (Test-Path $vcvars)) {
            throw 'cl.exe not found. Run this script from a VS2022 x64 Native Tools prompt.'
        }
        Write-Host "==> loading $vcvars"
        $envDump = & cmd.exe /c "`"$vcvars`" >nul 2>&1 && set"
        foreach ($line in $envDump) {
            if ($line -match '^([^=]+)=(.*)$') {
                [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
            }
        }
    }

    # 2. VS ships cmake/ninja but does not put them on PATH.
    $vsCommon = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake'
    foreach ($tool in @('CMake\bin', 'Ninja')) {
        $candidate = Join-Path $vsCommon $tool
        if ((Test-Path $candidate) -and ($env:PATH -notlike "*$candidate*")) {
            $env:PATH = "$candidate;$env:PATH"
        }
    }

    foreach ($tool in @('cmake', 'ninja', 'python')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
            throw "$tool not found on PATH."
        }
    }

    # 3. Configure, build, verify.
    Invoke-Native cmake @('-S', '.', '-B', $BuildDir, '-G', 'Ninja')
    Invoke-Native cmake @('--build', $BuildDir)

    $mxr = Join-Path $BuildDir 'mxr.exe'
    if (-not (Test-Path $mxr)) { throw "missing $mxr" }
    Invoke-Native $mxr @('selftest')

    if (-not $SkipE2E) {
        # Run e2e as a child process: its exit code is then unambiguous in $LASTEXITCODE.
        $pwsh = (Get-Process -Id $PID).Path
        Invoke-Native $pwsh @('-NoProfile', '-ExecutionPolicy', 'Bypass',
                              '-File', (Join-Path $repoRoot 'tests\e2e.ps1'))
    }

    Write-Host 'BUILD + TEST PASS'
}
catch {
    Write-Error $_
    exit 1
}
finally {
    Pop-Location
}
