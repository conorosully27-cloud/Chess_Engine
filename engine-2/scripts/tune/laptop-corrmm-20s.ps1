# 20+0.2 SPSA of the correction values (minor/major tables + the old ones),
# started from the 8+0.08 tune's values. Windows PowerShell, from engine-2\:
#   powershell -ExecutionPolicy Bypass -File scripts\tune\laptop-corrmm-20s.ps1 [-Workers 12]
# Interrupted? Run it again: it resumes from ~\spsa-corrmm-20s\state.json.
# fastchess: $env:FASTCHESS if set, else the first fastchess.exe under ~\fastchess-bin.
param([int]$Workers = 12)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..\..")
$dir = Join-Path $HOME "spsa-corrmm-20s"
New-Item -ItemType Directory -Force $dir | Out-Null

if (-not $env:FASTCHESS) {
  $fc = Get-ChildItem (Join-Path $HOME "fastchess-bin") -Recurse -Filter fastchess.exe -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $fc) { throw "fastchess.exe not found: set `$env:FASTCHESS to its full path" }
  $env:FASTCHESS = $fc.FullName
}
Write-Host "fastchess: $env:FASTCHESS"

$net = (Resolve-Path "nets\engine-7.3.nnue").Path
cmake -S . -B build-tune-win -G Ninja -DENGINE_TUNE=ON "-DENGINE_NET=$net" -DCMAKE_BUILD_TYPE=Release | Out-Null
if ($LASTEXITCODE) { throw "cmake configure failed" }
cmake --build build-tune-win | Out-Null
if ($LASTEXITCODE) { throw "build failed" }
$engine = Join-Path $dir "engine2.exe"
Copy-Item "build-tune-win\engine2.exe" $engine -Force
& $engine bench | Select-String "Nodes searched"   # expect 1180660

$state = Join-Path $dir "state.json"
if (-not (Test-Path $state)) {
  @'
{
 "theta": {
  "CORR_LIMIT_CP": 31,
  "CORR_MAX_WEIGHT": 14,
  "CORR_NON_PAWN_RATE": 22,
  "CORR_MINMAJ_RATE": 20,
  "CORR_MINMAJ_LIMIT_CP": 32
 },
 "pairs": 0, "wins": 0, "losses": 0, "draws": 0
}
'@ | Set-Content -Encoding ascii $state
}

$py = if (Get-Command py -ErrorAction SilentlyContinue) { "py" } else { "python" }
$log = Join-Path $dir "log.txt"
Write-Host "SPSA starting with $Workers games at a time; progress in $log (leave this window open)"
& $py scripts\tune\spsa.py --engine $engine --pairs 3000 --tc 20+0.2 --workers $Workers `
  --params CORR_MINMAJ_RATE,CORR_MINMAJ_LIMIT_CP,CORR_NON_PAWN_RATE,CORR_LIMIT_CP,CORR_MAX_WEIGHT `
  --r-end 0.005 --c-scale 2 --state $state *>> $log
