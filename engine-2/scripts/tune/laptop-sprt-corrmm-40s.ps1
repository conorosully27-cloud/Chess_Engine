# 40+0.4 SPRT [0, 5] on the laptop: 7.6 + minor/major correction tables with
# the 20+0.2 SPSA's values, against 7.6 (main). Windows PowerShell, from engine-2\:
#   powershell -ExecutionPolicy Bypass -File scripts\tune\laptop-sprt-corrmm-40s-76.ps1 [-Workers 12]
# Values: by default the average of the SPSA's last 500 pairs, read from
# ~\spsa-corrmm-20s\log.txt (as for the 8+0.08 tune); or give all five, e.g.
#   ... -LimitCp 29 -MaxWeight 16 -NonPawnRate 22 -MinMajRate 11 -MinMajLimitCp 31
# Builds both engines in git worktrees under ~\sprt-corrmm-40s-76 (your checkout
# is not touched): 7.6 from origin/main, the candidate from this branch.
# Interrupted? cd ~\sprt-corrmm-40s-76\run, then run fastchess -config file=config.json
param([int]$Workers = 12, [int]$LimitCp = 0, [int]$MaxWeight = 0, [int]$NonPawnRate = 0,
      [int]$MinMajRate = 0, [int]$MinMajLimitCp = 0)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..\..")
$here = (Get-Location).Path
$dir = Join-Path $HOME "sprt-corrmm-40s-76"
New-Item -ItemType Directory -Force $dir | Out-Null

if (-not $env:FASTCHESS) {
  $fc = Get-ChildItem (Join-Path $HOME "fastchess-bin") -Recurse -Filter fastchess.exe -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $fc) { throw "fastchess.exe not found: set `$env:FASTCHESS to its full path" }
  $env:FASTCHESS = $fc.FullName
}

# The five values.
$names = "CORR_LIMIT_CP", "CORR_MAX_WEIGHT", "CORR_NON_PAWN_RATE", "CORR_MINMAJ_RATE", "CORR_MINMAJ_LIMIT_CP"
$given = @($LimitCp, $MaxWeight, $NonPawnRate, $MinMajRate, $MinMajLimitCp)
$vals = @{}
if ($given -contains 0) {
  $log = Join-Path $HOME "spsa-corrmm-20s\log.txt"
  $rows = Get-Content $log | Where-Object { $_ -match '^pair (\d+)/3000' -and [int]$Matches[1] -gt 2500 }
  if (-not $rows) { throw "no reports past pair 2500 in $log; give all five values instead" }
  foreach ($n in $names) {
    $xs = $rows | ForEach-Object { if ($_ -match "$n=([\d.]+)") { [double]$Matches[1] } }
    $avg = ($xs | Measure-Object -Average).Average
    $vals[$n] = [int][math]::Round($avg)
    Write-Host ("{0}: average {1:N2} over {2} reports -> {3}" -f $n, $avg, $xs.Count, $vals[$n])
  }
} else {
  for ($i = 0; $i -lt 5; $i++) { $vals[$names[$i]] = $given[$i] }
}

# Worktrees and builds.
git fetch origin main claude/clever-hamilton-9d52iy
if ($LASTEXITCODE) { throw "git fetch failed" }
$net = (Resolve-Path "nets\engine-7.3.nnue").Path
function Build($name, $rev) {
  $wt = Join-Path $dir $name
  if (Test-Path $wt) { git worktree remove --force $wt | Out-Null }
  git worktree add --detach --force $wt $rev | Out-Null
  if ($LASTEXITCODE) { throw "git worktree add failed for $rev" }
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
$cand = Join-Path $dir "engine2-7.6-corrmm-20s.exe"
Compile (Build "wt-76" "origin/main") $base
$wt = Build "wt-cand" "origin/claude/clever-hamilton-9d52iy"
$search = Join-Path $wt "engine-2\src\search.cpp"
$text = Get-Content -Raw $search
foreach ($n in $names) {
  $new = $text -replace "TUNE_PARAM\($n, -?\d+,", "TUNE_PARAM($n, $($vals[$n]),"
  if ($new -eq $text -and $text -notmatch "TUNE_PARAM\($n, $($vals[$n]),") { throw "could not set $n" }
  $text = $new
}
Set-Content -NoNewline -Encoding ascii $search $text
Compile $wt $cand
Write-Host "7.6 bench (expect 1213141):"
& $base bench | Select-String "Nodes searched"
Write-Host "candidate bench:"
& $cand bench | Select-String "Nodes searched"

# Opening book and match.
$book = Join-Path $dir "openings.epd"
Get-Content (Join-Path $here "scripts\8moves_v3.epd") | Where-Object { $_ -and -not $_.StartsWith("#") } | Set-Content -Encoding ascii $book
$run = Join-Path $dir "run"
New-Item -ItemType Directory -Force $run | Out-Null
Set-Location $run
Write-Host "SPRT starting, $Workers games at a time; log in $dir\sprt.log (leave this window open)"
& $env:FASTCHESS -engine "cmd=$cand" name=engine2-7.6-corrmm-20s -engine "cmd=$base" name=engine2-7.6 `
  -each tc=40+0.4 -openings "file=$book" format=epd order=random `
  -rounds 10000 -repeat -concurrency $Workers -recover `
  -sprt elo0=0 elo1=5 alpha=0.05 beta=0.05 model=logistic `
  -resign movecount=3 score=600 twosided=true -draw movenumber=34 movecount=8 score=20 `
  -pgnout "file=$dir\games.pgn" *>> (Join-Path $dir "sprt.log")
