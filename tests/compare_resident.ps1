param(
    [Parameter(Mandatory)][string]$Baseline,
    [Parameter(Mandatory)][string]$Candidate,
    [Parameter(Mandatory)][string]$OutputDirectory
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
    $dir=$builds[$Label]
    $lines = & "$dir/tests/ui-review.exe" --bench
    if ($LASTEXITCODE) { throw 'UI benchmark failed' }
    $lines += & "$dir/tests/blur-bench.exe"
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
