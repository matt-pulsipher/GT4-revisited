# Builds the Windows release package from the sources tools/windows/send.sh put in the root folder,
# as the release workflow does: Release, SDL fetched and built as SDL3.dll, ANGLE's DLLs beside the
# game (from <root>\angle: libEGL.dll, libGLESv2.dll, LICENSE-ANGLE.txt, built by the engine's
# packaging/angle.ps1), no tests; installed into <root>\dist\<name> and zipped as <root>\<name>.zip.
#   powershell -NoProfile -ExecutionPolicy Bypass -File D:\gt4r\package.ps1 [-Name gt4r-windows-x64] [-Clean]
param([string]$Root = "D:\gt4r", [string]$Name = "gt4r-windows-x64", [switch]$Clean)
$ErrorActionPreference = "Stop"
$env:Path = "C:\Program Files\Git\cmd;C:\Program Files\CMake\bin;" + $env:Path
$src = "$Root\src"; $bld = "$Root\build-pkg"; $dist = "$Root\dist\$Name"
. "$PSScriptRoot\unpack.ps1"
if ($Clean -and (Test-Path $bld)) { Remove-Item -Recurse -Force $bld }
Update-Sources -Root $Root -Dest $src
$angle = ""
if (Test-Path "$Root\angle\libGLESv2.dll") { $angle = "-DGT3R_ANGLE_DIR=$Root\angle" } else { "no ANGLE in $Root\angle: the package goes without it" }
cmd /c "cmake -S $src -B $bld -G `"Visual Studio 17 2022`" -A x64 -DGT3R_FETCH_SDL=ON -DGT3R_SDL_SHARED=ON -DGT4R_BUILD_TESTS=OFF $angle > $Root\configure-pkg.log 2>&1"
if ($LASTEXITCODE) { Get-Content "$Root\configure-pkg.log" -Tail 20; throw "configure failed" }
cmd /c "cmake --build $bld --config Release --parallel --target gt4r gt4tool -- /m /v:minimal /nologo > $Root\build-pkg.log 2>&1"
if ($LASTEXITCODE) { Get-Content "$Root\build-pkg.log" | Where-Object { $_ -match "error" } | Select-Object -First 30; throw "build failed" }
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
cmd /c "cmake --install $bld --config Release --prefix $dist > $Root\install-pkg.log 2>&1"
if ($LASTEXITCODE) { Get-Content "$Root\install-pkg.log" -Tail 20; throw "install failed" }
$zip = "$Root\$Name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $dist -DestinationPath $zip
"package: $zip $([math]::Round((Get-Item $zip).Length/1MB,1)) MB, SHA256 $((Get-FileHash $zip -Algorithm SHA256).Hash.ToLower())"
Get-ChildItem -Recurse -File $dist | ForEach-Object { "  $($_.FullName.Substring($dist.Length + 1)) $([math]::Round($_.Length/1KB)) KB" }
