# Update-Sources: unpacks gt4r-src.tar.gz and gt4r-engine.tar.gz (tools/windows/send.sh) into a
# fresh staging folder, then brings the source folder to it: a file whose contents changed is copied
# and stamped with the present time, files gone from the sources are removed, the rest stay as they
# are. (An archive's files carry their commit's time, which can be older than the last build's
# objects: MSBuild would skip them.)
function Update-Sources([string]$Root, [string]$Dest, [switch]$Clean) {
    $stage = "$Root\stage"
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    New-Item -Force -ItemType Directory $stage | Out-Null
    tar -xzf "$Root\gt4r-src.tar.gz" -C $stage
    if ($LASTEXITCODE) { throw "unpacking gt4r-src.tar.gz failed" }
    tar -xzf "$Root\gt4r-engine.tar.gz" -C $stage
    if ($LASTEXITCODE) { throw "unpacking gt4r-engine.tar.gz failed" }
    if ($Clean -and (Test-Path $Dest)) { Remove-Item -Recurse -Force $Dest }
    New-Item -Force -ItemType Directory $Dest | Out-Null
    $now = Get-Date
    $changed = 0
    $staged = @{}
    Get-ChildItem -Recurse -File $stage | ForEach-Object {
        $rel = $_.FullName.Substring($stage.Length + 1)
        $staged[$rel] = $true
        $to = Join-Path $Dest $rel
        $same = (Test-Path $to) -and ((Get-Item $to).Length -eq $_.Length) -and
                ((Get-FileHash $to -Algorithm SHA256).Hash -eq (Get-FileHash $_.FullName -Algorithm SHA256).Hash)
        if (-not $same) {
            New-Item -Force -ItemType Directory (Split-Path $to) | Out-Null
            Copy-Item -Force $_.FullName $to
            (Get-Item $to).LastWriteTime = $now
            $changed++
        }
    }
    $removed = 0
    Get-ChildItem -Recurse -File $Dest | ForEach-Object {
        if (-not $staged.ContainsKey($_.FullName.Substring($Dest.Length + 1))) { Remove-Item -Force $_.FullName; $removed++ }
    }
    Remove-Item -Recurse -Force $stage
    "sources: $changed changed, $removed removed"
}
