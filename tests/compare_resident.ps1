param(
    [Parameter(Mandatory)][string]$Baseline,
    [Parameter(Mandatory)][string]$Candidate,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$BaselineSource
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
function PixelHash([string]$Path) {
    $image = [Drawing.Bitmap]::new($Path)
    try {
        $rect = [Drawing.Rectangle]::new(0,0,$image.Width,$image.Height)
        $data = $image.LockBits($rect,'ReadOnly',[Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $pixels = [byte[]]::new($image.Width*$image.Height*4)
            for ($row=0; $row -lt $image.Height; $row++) {
                $length = $image.Width*4
                [Runtime.InteropServices.Marshal]::Copy([IntPtr]::Add($data.Scan0,$row*$data.Stride),$pixels,$row*$length,$length)
            }
            return "$($image.Width)x$($image.Height) $([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($pixels)))"
        } finally { $image.UnlockBits($data) }
    } finally { $image.Dispose() }
}
$builds = [ordered]@{baseline=(Resolve-Path $Baseline).Path; candidate=(Resolve-Path $Candidate).Path}
$benchTools = @{baseline="$Baseline/tests"; candidate="$Candidate/tests"}
if ($BaselineSource) {
    # The accepted baseline predates the test drivers. Link the same drivers
    # against its optimized objects to measure the old implementation.
    $dir = $builds.baseline
    $src = (Resolve-Path $BaselineSource).Path
    $out = [IO.Directory]::CreateDirectory((Join-Path $OutputDirectory 'baseline-tools')).FullName
    $flags = @('/nologo','/O2','/MT','/std:c++17','/EHsc','/utf-8','/DUNICODE','/D_UNICODE','/DISTAROTH_EMBED_ICON',
        '/FIistaroth_imgui_config.h',"/I$src/src/shared","/I$src/src/resident","/I$src/third_party/imgui","/I$dir")
    $objects = 'resident_ui','imgui','imgui_draw','imgui_widgets','imgui_tables','imgui_impl_dx11','imgui_impl_win32' |
        ForEach-Object { "$dir/$_.obj" }
    & cl.exe @flags "$PSScriptRoot/ui_review.cpp" "/Fo:$out/review.obj" "/Fe:$out/ui-review.exe" @objects d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib imm32.lib dwmapi.lib windowscodecs.lib ole32.lib
    if ($LASTEXITCODE) { throw 'Baseline UI driver build failed' }
    & cl.exe @flags "$PSScriptRoot/blur_bench.cpp" "/Fo:$out/bench.obj" "/Fe:$out/blur-bench.exe" d3d11.lib
    if ($LASTEXITCODE) { throw 'Baseline blur driver build failed' }
    & "$out/ui-review.exe" --test
    if ($LASTEXITCODE) { throw 'Baseline menu interaction test failed' }
    $benchTools.baseline = $out
}
$hashes = @{}
$timings = @{}
foreach ($label in $builds.Keys) {
    $dir = $builds[$label]
    $out = [IO.Directory]::CreateDirectory((Join-Path $OutputDirectory $label)).FullName
    $images = [ordered]@{}
    foreach ($mode in 'hud-paused','hud-active','paused','waiting','active','confirming','restoring','fault','starting','opening','closing','mixed','ready') {
        $image = Join-Path $out "$mode.png"
        & "$dir/tests/ui-snapshot.exe" 1280 800 $mode $image "$dir/assets/istaroth-mark.png" | Out-Null
        if ($LASTEXITCODE) { throw "Snapshot failed for $mode ($label)" }
        $images[$mode] = PixelHash $image
    }
    foreach ($name in '00-dev1-srgb','01-corrected-color','02-glass-balanced','03-glass-strong') {
        $images[$name] = PixelHash "$dir/tests/material-previews/$name.png"
    }
    $hashes[$label]=$images
    $timings[$label]=@{hud_ms=@();menu_ms=@();first_draw_cpu_median_ms=@()}
}
foreach ($name in $hashes.baseline.Keys) {
    if ($hashes.baseline[$name] -ne $hashes.candidate[$name]) { throw "Pixel mismatch: $name" }
}
function MeasureGroup([string]$Label) {
    $dir=$benchTools[$Label]
    $lines = & "$dir/ui-review.exe" --bench
    if ($LASTEXITCODE) { throw 'UI benchmark failed' }
    $lines += & "$dir/blur-bench.exe"
    if ($LASTEXITCODE) { throw 'Material benchmark failed' }
    foreach ($line in $lines) {
        if ($line -notmatch '^(\w+_ms)=([\d.]+)') { throw "Unexpected benchmark output $line" }
        $timings[$Label][$Matches[1]]+= [double]::Parse($Matches[2],[Globalization.CultureInfo]::InvariantCulture)
    }
}
for ($attempt=1; $attempt -le 2; $attempt++) {
    foreach ($series in $timings.Values) { foreach ($key in @($series.Keys)) { $series[$key]=@() } }
    for ($round=0; $round -lt 7; $round++) {
        $order = if ($round%2) {'candidate','baseline'} else {'baseline','candidate'}
        foreach ($label in $order) { MeasureGroup $label }
    }
    $summary = foreach ($metric in $timings.baseline.Keys) {
        $old=@($timings.baseline[$metric] | Sort-Object)
        $new=@($timings.candidate[$metric] | Sort-Object)
        if ($old.Length -ne 7 -or $new.Length -ne 7) { throw "Missing samples: $metric" }
        $limit=$old[3]+[Math]::Max(.2,.05*$old[3])
        [pscustomobject]@{metric=$metric;baseline_ms=$old[3];candidate_ms=$new[3];limit_ms=$limit;passed=$new[3] -le $limit}
    }
    $report=[ordered]@{pixels=$hashes;performance=$summary;samples=$timings;attempt=$attempt}
    $report | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $OutputDirectory "comparison-$attempt.json")
    $summary | Format-Table
    if (!($summary | Where-Object { !$_.passed })) { return }
}
throw 'Repeated performance regression'
