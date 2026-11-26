# Runs the MSVC build's unit tests (tools/windows/build.ps1), with the discs in <root>\discs when
# -Discs is given (every .iso there, as GT4R_DISC), writing <root>\tests[-discs].log; prints the
# summary and every FAIL line.
#   powershell -NoProfile -ExecutionPolicy Bypass -File D:\gt4r\test.ps1 [-Discs] [-Filter name]
param([string]$Root = "D:\gt4r", [switch]$Discs, [string]$Filter = "", [string]$Config = "Release")
$exe = "$Root\build\$Config\gt4r_tests.exe"
$env:SDL_AUDIODRIVER = "dummy"
$env:SDL_VIDEODRIVER = "dummy"
$log = "$Root\tests.log"
if ($Discs) {
    $env:GT4R_DISC = (Get-ChildItem "$Root\discs\*.iso" | ForEach-Object { $_.FullName }) -join ';'
    $log = "$Root\tests-discs.log"
    "discs: $env:GT4R_DISC"
} else {
    Remove-Item Env:GT4R_DISC -ErrorAction SilentlyContinue
}
# Through cmd: PowerShell 5 stops on a native program's standard error otherwise.
$t0 = Get-Date
cmd /c "$exe $Filter > $log 2>&1" # (the root has no spaces)
$code = $LASTEXITCODE
"exit $code after $([math]::Round(((Get-Date) - $t0).TotalSeconds)) s"
Get-Content $log | Where-Object { $_ -match "^FAIL|  FAIL| run, .* skipped" } | Select-Object -Last 60
