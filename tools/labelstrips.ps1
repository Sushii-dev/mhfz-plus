# Captures 24 displayed frames of a test-app scene with each build and stacks a crop of them into one image per build
# (build\strip-<name>.png): what the in-between frames do to fast-moving screen text.
#   tools\labelstrips.ps1 -Builds old=build\d3d9.tickfix.dll,new=build\d3d9.dll [-Scene "3 0 0 0 4"] [-Crop "0,250,800,370"] [-From 4] [-Count 14]
param([string]$Builds, [string]$Scene = "3 0 0 0 4", [string]$Crop = "0,250,800,370", [int]$From = 4, [int]$Count = 14, [string]$Ini = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
foreach ($b in ($Builds -split ',')) {
    $name, $dll = $b -split '='
    $w = Join-Path $root "build\cap-$name"
    Remove-Item -Recurse -Force $w -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $w | Out-Null
    Copy-Item "$root\build\testapp.exe" $w
    Copy-Item (Join-Path $root $dll) "$w\d3d9.dll"
    Set-Content "$w\mhfz-fps.ini" ("[fps]`r`n" + ($Ini -replace ';', "`r`n") + "`r`nmode=2`r`ntarget_fps=120`r`npresent_interval=0`r`nstats_interval=1`r`ncapture_at_start=24`r`n")
    Start-Process -NoNewWindow "$w\testapp.exe" -ArgumentList ($Scene -split ' ') -WorkingDirectory $w -Wait
    python -c @"
from PIL import Image
import glob
fs = sorted(glob.glob(r'$w\captures\*.png'))[${From}:${From} + ${Count}]
box = tuple(int(v) for v in '$Crop'.split(','))
ims = [Image.open(f).convert('RGB').crop(box) for f in fs]
W, H = box[2] - box[0], box[3] - box[1]
out = Image.new('RGB', (W, H * len(ims)))
for k, im in enumerate(ims): out.paste(im, (0, H * k))
out.save(r'$root\build\strip-$name.png')
print('$name', len(fs), 'frames:', ' '.join(f.split('\\')[-1][5:-4] for f in fs))
"@
    Select-String -Path "$w\mhfz-fps.log" -Pattern '  text:' | Select-Object -Last 1 | ForEach-Object { "  " + $_.Line.Substring(13) }
}
