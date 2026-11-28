# 40+0.4 SPRT [0, 5] on the laptop: 7.6 with the 13 values from the 20+0.2
# SPSA (average of its last 1,000 pairs) against 7.6. Windows PowerShell,
# from engine-2\ on the claude/clever-hamilton-9d52iy branch:
#   powershell -ExecutionPolicy Bypass -File scripts\tune\laptop-sprt-76-spsa20.ps1 [-Workers 12]
# Both engines are built from the same 7.6 commit the SPSA tuned (pinned in
# ~\spsa-76-20s\commit.txt), in worktrees under ~\sprt-76-spsa20.
# Interrupted? cd ~\sprt-76-spsa20\run, then run fastchess -config file=config.json
param([int]$Workers = 12)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..\..")
$here = (Get-Location).Path
$dir = Join-Path $HOME "sprt-76-spsa20"
New-Item -ItemType Directory -Force $dir | Out-Null

if (-not $env:FASTCHESS) {
  $fc = Get-ChildItem (Join-Path $HOME "fastchess-bin") -Recurse -Filter fastchess.exe -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $fc) { throw "fastchess.exe not found: set `$env:FASTCHESS to its full path" }
  $env:FASTCHESS = $fc.FullName
}

# Average of the SPSA's last 1,000 pairs, rounded.
$vals = [ordered]@{
  RFP_MARGIN = 61; FUT_MARGIN = 80; LMP_SCALE = 79; SEE_CAPTURE_MARGIN = 80;
  LMR_BASE = 126; LMR_DIV = 339; LMR_HISTORY = 6446; NULL_EVAL_DIV = 22;
  HIST_PRUNE_MARGIN = 4916; SINGULAR_MARGIN = 12; DOUBLE_EXT_MARGIN = 11;
  PROBCUT_MARGIN = 139; RESEARCH_DEEPER_CP = 46
}

$pin = Join-Path $HOME "spsa-76-20s\commit.txt"
if (Test-Path $pin) { $commit = (Get-Content $pin).Trim() }
else { git fetch origin main; $commit = (git rev-parse origin/main).Trim() }
Write-Host "7.6 commit: $commit"
$net = (Resolve-Path "nets\engine-7.3.nnue").Path

function New-Tree($name) {
  $wt = Join-Path $dir $name
  if (Test-Path $wt) { git worktree remove --force $wt | Out-Null }
  git worktree add --detach --force $wt $commit | Out-Null
  if ($LASTEXITCODE) { throw "git worktree add failed for $commit" }
  return $wt
}
function Compile($wt, $exe) {
  $src = Join-Path $wt "engine-2"
  cmake -S $src -B (Join-Path $src "build") -G Ninja "-DENGINE_NET=$net" -DCMAKE_BUILD_TYPE=Release | Out-Null
  if ($LASTEXITCODE) { throw "cmake configure failed in $src" }
  cmake --build (Join-Path $src "build") | Out-Null
  if ($LASTEXITCODE) { throw "build failed in $src" }
  Copy-Item (Join-Path $src "build\engine2.exe") $exe -Force
}
$base = Join-Path $dir "engine2-7.6.exe"
$cand = Join-Path $dir "engine2-7.6-spsa20.exe"
Compile (New-Tree "wt-base") $base
$wt = New-Tree "wt-cand"
$search = Join-Path $wt "engine-2\src\search.cpp"
$text = Get-Content -Raw $search
foreach ($n in $vals.Keys) {
  if ($text -notmatch "TUNE_PARAM\($n, -?\d+,") { throw "could not find $n" }
  $text = $text -replace "TUNE_PARAM\($n, -?\d+,", "TUNE_PARAM($n, $($vals[$n]),"
}
Set-Content -NoNewline -Encoding ascii $search $text
Compile $wt $cand
Write-Host "7.6 bench (expect 1213141):"
& $base bench | Select-String "Nodes searched"
Write-Host "candidate bench (expect 876266):"
& $cand bench | Select-String "Nodes searched"

$book = Join-Path $dir "openings.epd"
Get-Content (Join-Path $here "scripts\8moves_v3.epd") | Where-Object { $_ -and -not $_.StartsWith("#") } | Set-Content -Encoding ascii $book
$run = Join-Path $dir "run"
New-Item -ItemType Directory -Force $run | Out-Null
Set-Location $run
Write-Host "SPRT starting, $Workers games at a time; log in $dir\sprt.log (leave this window open)"
& $env:FASTCHESS -engine "cmd=$cand" name=engine2-7.6-spsa20 -engine "cmd=$base" name=engine2-7.6 `
  -each tc=40+0.4 -openings "file=$book" format=epd order=random `
  -rounds 10000 -repeat -concurrency $Workers -recover `
  -sprt elo0=0 elo1=5 alpha=0.05 beta=0.05 model=logistic `
  -resign movecount=3 score=600 twosided=true -draw movenumber=34 movecount=8 score=20 `
  -pgnout "file=$dir\games.pgn" *>> (Join-Path $dir "sprt.log")
