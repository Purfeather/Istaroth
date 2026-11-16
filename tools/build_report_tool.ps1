param(
    [string]$VcTools = $env:VCToolsInstallDir,
    [string]$SdkRoot = $env:WindowsSdkDir,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../out/report-tool')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
$lock = Get-Content (Join-Path $root 'toolchain.json') -Raw | ConvertFrom-Json
if (-not $VcTools) { throw 'Run in an x64 developer shell or pass -VcTools <MSVC Tools/version directory>.' }
$VcTools = [IO.Path]::GetFullPath($VcTools).TrimEnd('\','/')
if ((Split-Path $VcTools -Leaf) -ne $lock.msvc_tools_version) { throw "Expected MSVC $($lock.msvc_tools_version)." }
if (-not $SdkRoot) { $SdkRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10 }
$sdkVersion = $lock.windows_sdk_version
$compiler = Join-Path $VcTools 'bin/Hostx64/x64/cl.exe'
if (-not (Test-Path $compiler)) { throw 'Pinned MSVC compiler not found.' }
if (-not (Test-Path (Join-Path $SdkRoot "Include/$sdkVersion/um/windows.h"))) { throw "Install Windows SDK $sdkVersion." }
$env:INCLUDE = "$VcTools/include;$SdkRoot/Include/$sdkVersion/ucrt;$SdkRoot/Include/$sdkVersion/shared;$SdkRoot/Include/$sdkVersion/um;$SdkRoot/Include/$sdkVersion/winrt"
$env:LIB = "$VcTools/lib/x64;$SdkRoot/Lib/$sdkVersion/ucrt/x64;$SdkRoot/Lib/$sdkVersion/um/x64"
$env:PATH = "$VcTools/bin/Hostx64/x64;$SdkRoot/bin/$sdkVersion/x64;$env:PATH"
$out = [IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($out)
function Run([string]$File, [string[]]$Arguments) {
    & $File @Arguments | Out-File -LiteralPath "$out/build.log" -Append -Encoding utf8
    if ($LASTEXITCODE) { throw "Command failed ($LASTEXITCODE): $File; see $out/build.log" }
}
Push-Location $root
try {
    '' | Set-Content -LiteralPath "$out/build.log"
    $revision = & git -c "safe.directory=$root" rev-parse HEAD 2>$null
    if ($LASTEXITCODE -or $revision -notmatch '^[0-9a-f]{40}$') { $revision = 'unknown'; $dirty = 1 }
    else { $dirty = [int][bool](& git -c "safe.directory=$root" status --porcelain) }
    # The collector has its own version; it does not identify the installed game tool.
    [IO.File]::WriteAllLines("$out/build_identity.h",@('#pragma once','#define ISTAROTH_BUILD_VERSION "0.1.0"',"#define ISTAROTH_SOURCE_REVISION `"$revision`"","#define ISTAROTH_SOURCE_DIRTY $dirty"))
    $flags = @('/nologo','/std:c++17','/EHsc','/W4','/MT','/O2','/utf-8','/DUNICODE','/D_UNICODE','/Isrc/loader','/Isrc/shared',('/I' + $out),('/Fo:' + $out + '/'))
    $libraries = @('user32.lib','gdi32.lib','shell32.lib','ole32.lib','uuid.lib')
    Run 'rc.exe' @('/nologo',('/fo' + "$out/report_tool.res"),'src/loader/report_tool.rc')
    Run $compiler ($flags + @('src/loader/report_tool.cpp',"$out/report_tool.res",('/Fe:' + "$out/伊斯塔露报告工具.exe")) + $libraries + @('/link','/INCREMENTAL:NO','/SUBSYSTEM:WINDOWS','/MANIFEST:EMBED',"/MANIFESTUAC:level='asInvoker' uiAccess='false'"))
    Run $compiler ($flags + @('tests/report_tool_test.cpp',"$out/report_tool.res",('/Fe:' + "$out/report-tool-test.exe")) + $libraries + @('/link','/INCREMENTAL:NO'))
    Run $compiler ($flags + @('tests/diagnostics_test.cpp',('/Fe:' + "$out/diagnostics-test.exe")))
    Run "$out/report-tool-test.exe" @()
    Run "$out/diagnostics-test.exe" @()
    Write-Output "Built and tested standalone collector: $out/伊斯塔露报告工具.exe"
} finally { Pop-Location }
