param(
    [string]$VcTools,
    [string]$SdkRoot,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'out'),
    [switch]$SkipTests,
    [switch]$Slim
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = $PSScriptRoot
$lock = Get-Content (Join-Path $root 'toolchain.json') -Raw | ConvertFrom-Json
if (-not $VcTools) {
    if ($env:VCToolsInstallDir) { $VcTools = $env:VCToolsInstallDir }
    else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Use -VcTools <MSVC Tools/version directory> or an x64 developer shell.' }
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $VcTools = Join-Path $installation ('VC/Tools/MSVC/' + $lock.msvc_tools_version)
    }
}
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
foreach ($directory in @($out,"$out/detours","$out/tests","$out/tests/bootstrap","$out/assets")) { [void][IO.Directory]::CreateDirectory($directory) }
function Run([string]$File, [string[]]$Arguments) {
    & $File @Arguments | Out-File -LiteralPath (Join-Path $out 'build.log') -Append -Encoding utf8
    if ($LASTEXITCODE) { throw "Build command failed ($LASTEXITCODE): $File; see out/build.log." }
}
$include = @('src/loader','src/resident','src/features','src/shared','profiles','third_party/imgui','third_party/detours/src') | ForEach-Object { '/I' + (Join-Path $root $_) }
$include += '/I' + $out
$flags = @('/nologo','/W4','/MT','/O2','/utf-8','/DUNICODE','/D_UNICODE') + $include
function CompileCpp([string]$Name,[string[]]$Arguments) {
    Run $compiler (@('/std:c++17','/EHsc') + $flags + @(('/Fo:' + $out + '/'), ('/Fe:' + (Join-Path $out $Name))) + $Arguments)
}
function CompileC([string]$Name,[string[]]$Arguments) {
    Run $compiler (@('/std:c11') + $flags + @(('/Fo:' + $out + '/'), ('/Fe:' + (Join-Path $out $Name))) + $Arguments)
}
Push-Location $root
try {
    '' | Set-Content -LiteralPath (Join-Path $out 'build.log')
    $detoursSources = @('detours','modules','disasm','image','creatwth','disolx86','disolx64','disolia64','disolarm','disolarm64')
    foreach ($name in $detoursSources) {
        Run $compiler @('/nologo','/c','/O2','/MT','/EHsc',('/Fo:' + "$out/detours/$name.obj"),("third_party/detours/src/$name.cpp"))
    }
    $detours = Join-Path $out 'detours/detours.lib'
    Run (Join-Path $VcTools 'bin/Hostx64/x64/lib.exe') (@('/nologo',('/OUT:' + $detours)) + @($detoursSources | ForEach-Object { "$out/detours/$_.obj" }))
    CompileC 'time_core.dll' @('/LD','src/features/time_core.c')
    CompileC 'story_probe.dll' @('/LD','src/features/story_probe.c')
    CompileC 'tests/time_core_test.dll' @('/LD','/DGST_SELF_TEST','src/features/time_core.c')
    CompileC 'tests/story_probe_test.dll' @('/LD','/DGST_STORY_TEST','src/features/story_probe.c')
    CompileC 'tests/gst-time-test.exe' @('tests/time_host.c')
    CompileC 'tests/gst-story-test.exe' @('tests/story_host.c')
    $assembly = Join-Path $out 'dialogue_cooldown.obj'
    Run (Join-Path $VcTools 'bin/Hostx64/x64/ml64.exe') @('/nologo','/c',('/Fo' + $assembly),'src/features/dialogue_cooldown.asm')
    CompileCpp 'dialogue_actions.dll' @('/LD','src/features/dialogue_actions.cpp','/link','/INCREMENTAL:NO',$assembly,$detours,'user32.lib')
    CompileCpp 'tests/dialogue_actions_test.dll' @('/LD','/DISTAROTH_ACTIONS_TEST','src/features/dialogue_actions.cpp','/link','/INCREMENTAL:NO',$assembly,$detours,'user32.lib')
    $text = ''
    foreach ($name in @('resident_ui.cpp','resident.cpp','resident_control.h')) { $text += [IO.File]::ReadAllText((Join-Path $root "src/resident/$name")) }
    $codes = @($text.ToCharArray() | ForEach-Object { [int]$_ } | Where-Object { $_ -gt 127 -and $_ -lt 65535 } | Sort-Object -Unique)
    $ranges = @('32','126'); foreach ($code in $codes) { $ranges += @([string]$code,[string]$code) }; $ranges += '0'
    [IO.File]::WriteAllText("$out/resident_glyphs.h",('static const ImWchar ResidentGlyphs[]={' + ($ranges -join ',') + '};'))
    $uiSources = @('src/resident/resident_ui.cpp','third_party/imgui/imgui.cpp','third_party/imgui/imgui_draw.cpp','third_party/imgui/imgui_tables.cpp','third_party/imgui/imgui_widgets.cpp','third_party/imgui/backends/imgui_impl_win32.cpp','third_party/imgui/backends/imgui_impl_dx11.cpp')
    $uiLibraries = @('d3d11.lib','dxgi.lib','d3dcompiler.lib','user32.lib','gdi32.lib','imm32.lib','dwmapi.lib','windowscodecs.lib','ole32.lib')
    $slimFlags = @()
    if ($Slim) {
        Run 'python' @('-B','tools/embed_icon.py',$root,$out)
        $slimFlags = @('/DISTAROTH_EMBED_ICON')
    }
    CompileCpp 'Istaroth.dll' (@('/LD','src/resident/resident.cpp','src/resident/resident_renderer.cpp') + $slimFlags + $uiSources + @('/link','/INCREMENTAL:NO',$detours) + $uiLibraries)
    $profile = Get-Content 'profiles/compatibility.json' -Raw | ConvertFrom-Json
    $payloadNames = @('Istaroth.dll','time_core.dll','story_probe.dll','dialogue_actions.dll')
    $header = @('#pragma once','struct Payload { const wchar_t* name; const char* sha256; };',('static const wchar_t* TargetName=L"' + $profile.executable + '";'),('static const char* TargetSha256="' + $profile.sha256 + '";'),'static const wchar_t* ResidentPayload=L"Istaroth.dll";','static const Payload Payloads[]={')
    foreach ($name in $payloadNames) { $hash=(Get-FileHash "$out/$name").Hash.ToLowerInvariant(); $header += '{L"' + $name + '","' + $hash + '"},' }; $header += '};'
    [IO.File]::WriteAllLines("$out/resident_payloads.h",$header)
    Run 'rc.exe' @('/nologo',('/i' + "$root/assets"),('/fo' + "$out/injector.res"),'src/loader/injector.rc')
    $loaderLibraries = @('bcrypt.lib','psapi.lib','shell32.lib','user32.lib','gdi32.lib')
    CompileCpp 'Istaroth.exe' (@('src/loader/istaroth_injector.cpp','src/loader/injector_ui.cpp',"$out/injector.res") + $loaderLibraries + @('/link','/SUBSYSTEM:WINDOWS','/MANIFEST:EMBED',"/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"))
    Copy-Item 'assets/istaroth-mark.png' "$out/assets/istaroth-mark.png" -Force
    CompileCpp 'tests/control-test.exe' @('tests/control_test.cpp')
    CompileCpp 'tests/cursor-test.exe' @('tests/cursor_test.cpp','user32.lib')
    CompileCpp 'tests/loader-test.exe' (@('tests/loader_test.cpp','src/loader/injector_ui.cpp',"$out/injector.res") + $loaderLibraries)
    # Bootstrap fixtures use dummy hash-pinned files, never real game discovery.
    Run 'python' @('-B','tools/bootstrap_fixture.py',"$out/tests/bootstrap")
    $bootstrapInclude = '/I' + "$out/tests/bootstrap"
    $bootstrapFlags = @('/nologo','/std:c++17','/EHsc','/MT','/O2','/utf-8','/DUNICODE','/D_UNICODE',$bootstrapInclude) + $include
    Run $compiler ($bootstrapFlags + @('tests/bootstrap_test.cpp','src/loader/injector_ui.cpp',('/Fo:' + "$out/tests/bootstrap/"),('/Fe:' + "$out/tests/bootstrap/bootstrap-test.exe")) + $loaderLibraries)
    CompileCpp 'tests/actions-test.exe' @('tests/actions_test.cpp')
    $uiObjects = @('resident_ui','imgui','imgui_draw','imgui_widgets','imgui_tables','imgui_impl_dx11') | ForEach-Object { "$out/$_.obj" }
    CompileCpp 'tests/ui-snapshot.exe' (@('tests/ui_snapshot.cpp') + $slimFlags + $uiObjects + $uiLibraries)
    Run 'python' @('-B','tools/build_manifest.py',$root,$out,($(if ($Slim) {'0.1.1-candidate'} else {(Get-Content 'VERSION' -Raw).Trim()})))
    if (-not $SkipTests) { Run 'python' @('-B','tests/run.py',$out) }
    Write-Output "Built all five native binaries from source: $out"
} finally { Pop-Location }
