# Builds GT4 Revisited natively with MSVC (Visual Studio 2022 or its Build Tools) from the sources
# tools/windows/send.sh put in the root folder: gt4r, gt4tool, gt4r_diff and gt4r_tests, with a
# static SDL (the tests run without SDL3.dll beside them).
#   powershell -NoProfile -ExecutionPolicy Bypass -File D:\gt4r\build.ps1 [-Clean] [-Config Release]
param([string]$Root = "D:\gt4r", [switch]$Clean, [string]$Config = "Release",
      [string]$Targets = "gt4r gt4tool gt4r_diff gt4r_tests")
$ErrorActionPreference = "Stop"
$env:Path = "C:\Program Files\Git\cmd;C:\Program Files\CMake\bin;" + $env:Path
$src = "$Root\src"; $bld = "$Root\build"
. "$PSScriptRoot\unpack.ps1"
if ($Clean -and (Test-Path $bld)) { Remove-Item -Recurse -Force $bld }
Update-Sources -Root $Root -Dest $src -Clean:$Clean

cmd /c "cmake -S $src -B $bld -G `"Visual Studio 17 2022`" -A x64 > $Root\configure.log 2>&1"
if ($LASTEXITCODE) { Get-Content "$Root\configure.log" -Tail 20; throw "configure failed" }
$t = ($Targets -split ' ' | ForEach-Object { "--target $_" }) -join ' '
cmd /c "cmake --build $bld --config $Config --parallel $t -- /m /v:minimal /nologo > $Root\build.log 2>&1"
$failed = $LASTEXITCODE
# Every distinct warning and error of our own code (the engine's and the fetched libraries' apart).
$lines = Get-Content "$Root\build.log" | Where-Object { $_ -match ": (error|warning) [A-Z]+\d+" } | Sort-Object -Unique
"warnings and errors: $($lines.Count) (ours: $(($lines | Where-Object { $_ -match '\\src\\src\\gt4r\\|\\src\\tools\\|\\src\\tests\\' }).Count))"
$lines | Where-Object { $_ -match '\\src\\src\\gt4r\\|\\src\\tools\\|\\src\\tests\\' } | Select-Object -First 80
if ($failed) { Get-Content "$Root\build.log" | Where-Object { $_ -match "error" } | Select-Object -First 30; throw "build failed" }
"built:"
Get-ChildItem "$bld\$Config\*.exe" | ForEach-Object { "  $($_.Name) $([math]::Round($_.Length/1MB,1)) MB" }
