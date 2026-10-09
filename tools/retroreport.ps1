# One rolling-recorder save (fps-state\rain\retro\NNN, Delete key with retro=1) analysed in one go:
#  - flickerscan.py over its frames (spikes, heat map, worst events as side-by-side crops)
#  - a contact sheet of the last 48 frames before the press (sheet.png)
#  - pairbench -seq over ticks.seq: per-tick matching statistics, with -detail for the last 8 ticks
#   tools\retroreport.ps1 [-Dir <retro\NNN>]   (default: the newest save)
param([string]$Dir = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
if (-not $Dir) {
    $base = Join-Path $env:LOCALAPPDATA "MHFZ-Plus\fps-state\rain\retro"
    $Dir = (Get-ChildItem $base -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
}
"== $Dir"
$frames = @(Get-ChildItem $Dir -Filter "*.jpg" | Where-Object { $_.Name -notmatch '^flicker_' } | Sort-Object Name)
"{0} frames, ticks {1} .. {2}" -f $frames.Count, ($frames[0].Name -replace '^\d+_tick(\d+)_.*$', '$1'), ($frames[-1].Name -replace '^\d+_tick(\d+)_.*$', '$1')
python (Join-Path $PSScriptRoot "flickerscan.py") $Dir -top 8
python -c @"
from PIL import Image
import glob, os
fs = sorted(f for f in glob.glob(os.path.join(r'$Dir', '*.jpg')) if not os.path.basename(f).startswith('flicker_'))[-48:]
ims = [Image.open(f).convert('RGB') for f in fs]
w, h = ims[0].size
tw, th = w // 4, h // 4
out = Image.new('RGB', (tw * 6, th * 8), (255, 0, 255))
for k, im in enumerate(ims):
    out.paste(im.resize((tw, th)), ((k % 6) * tw, (k // 6) * th))
out.save(os.path.join(r'$Dir', 'sheet.png'))
print('sheet.png: last', len(fs), 'frames, 6 per row')
"@
$seq = Join-Path $Dir "ticks.seq"
if (Test-Path $seq) {
    $bench = Join-Path $root "build\pairbench.exe"
    & $bench -seq $seq 2>&1 | Out-String
    "--- details, last ticks"
    & $bench -seq $seq -detail 2>&1 | Select-Object -Last 48 | Out-String
}
