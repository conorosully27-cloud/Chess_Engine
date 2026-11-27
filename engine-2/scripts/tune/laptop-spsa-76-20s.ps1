# 20+0.2 SPSA of 13 search values on 7.6 (main): the reduction, extension and
# pruning values most likely to be off at long time controls or on the newer
# networks (all last tuned at 8+0.08, mostly on 6.6). Windows PowerShell,
# from engine-2\ on the claude/clever-hamilton-9d52iy branch:
#   powershell -ExecutionPolicy Bypass -File scripts\tune\laptop-spsa-76-20s.ps1 [-Workers 12] [-Pairs 6000]
# Interrupted? Run the same command again: it resumes from
# ~\spsa-76-20s\state.json with the same engine (the 7.6 commit is pinned
# in ~\spsa-76-20s\commit.txt on the first run, so a newer main is ignored).
# When it ends, test the average of the last 1,000 pairs (log.txt).
param([int]$Workers = 12, [int]$Pairs = 6000)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..\..")
$dir = Join-Path $HOME "spsa-76-20s"
New-Item -ItemType Directory -Force $dir | Out-Null

if (-not $env:FASTCHESS) {
  $fc = Get-ChildItem (Join-Path $HOME "fastchess-bin") -Recurse -Filter fastchess.exe -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $fc) { throw "fastchess.exe not found: set `$env:FASTCHESS to its full path" }
  $env:FASTCHESS = $fc.FullName
}
Write-Host "fastchess: $env:FASTCHESS"

# The engine: a tuning build of 7.6, in a worktree (your checkout is not touched).
$engine = Join-Path $dir "engine2.exe"
$pin = Join-Path $dir "commit.txt"
if (-not (Test-Path $pin)) {
  git fetch origin main
  if ($LASTEXITCODE) { throw "git fetch failed" }
  git rev-parse origin/main | Set-Content -Encoding ascii $pin
}
$commit = (Get-Content $pin).Trim()
if (-not (Test-Path $engine)) {
  $wt = Join-Path $dir "wt"
  if (Test-Path $wt) { git worktree remove --force $wt | Out-Null }
  git worktree add --detach --force $wt $commit | Out-Null
  if ($LASTEXITCODE) { throw "git worktree add failed for $commit" }
  $src = Join-Path $wt "engine-2"
  $net = (Resolve-Path "nets\engine-7.3.nnue").Path
  cmake -S $src -B (Join-Path $src "build-tune") -G Ninja -DENGINE_TUNE=ON "-DENGINE_NET=$net" -DCMAKE_BUILD_TYPE=Release | Out-Null
  if ($LASTEXITCODE) { throw "cmake configure failed" }
  cmake --build (Join-Path $src "build-tune") | Out-Null
  if ($LASTEXITCODE) { throw "build failed" }
  Copy-Item (Join-Path $src "build-tune\engine2.exe") $engine -Force
}
Write-Host "engine: 7.6 tuning build of $commit, bench (expect 1213141):"
& $engine bench | Select-String "Nodes searched"

$params = "LMR_BASE,LMR_DIV,LMR_HISTORY,SINGULAR_MARGIN,DOUBLE_EXT_MARGIN,RESEARCH_DEEPER_CP," +
          "RFP_MARGIN,FUT_MARGIN,LMP_SCALE,SEE_CAPTURE_MARGIN,PROBCUT_MARGIN,NULL_EVAL_DIV,HIST_PRUNE_MARGIN"
$py = if (Get-Command py -ErrorAction SilentlyContinue) { "py" } else { "python" }
$log = Join-Path $dir "log.txt"
Write-Host "SPSA: $Pairs pairs at 20+0.2, $Workers games at a time; progress in $log (leave this window open)"
& $py scripts\tune\spsa.py --engine $engine --pairs $Pairs --tc 20+0.2 --workers $Workers `
  --params $params --r-end 0.02 --c-scale 2 --state (Join-Path $dir "state.json") *>> $log
