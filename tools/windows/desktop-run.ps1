# Runs a .cmd in the signed-in user's desktop session (a one-off scheduled task) and waits for it
# to end: programs started over SSH have no desktop, so the game could not open its window or use
# the GPU there.
#   powershell -NoProfile -ExecutionPolicy Bypass -File D:\gt4r\desktop-run.ps1 -Script D:\gt4r\title-shot.cmd -ScriptArgs "..." [-WaitSeconds 120]
param([Parameter(Mandatory)][string]$Script, [string]$ScriptArgs = "", [int]$WaitSeconds = 120,
      [string]$Task = "gt4r-windows-desktop-run")
$user = ((query user) -match "Active" | Select-Object -First 1) -replace "^\s*>?(\S+).*", '$1'
if (-not $user) { throw "nobody is signed in on the desktop" }
$action = New-ScheduledTaskAction -Execute "cmd.exe" -Argument "/c $Script $ScriptArgs"
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive
Register-ScheduledTask -TaskName $Task -Action $action -Principal $principal -Force | Out-Null
Start-ScheduledTask -TaskName $Task
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt $WaitSeconds) {
    Start-Sleep -Seconds 2
    if ((Get-ScheduledTask -TaskName $Task).State -ne "Running" -and ((Get-Date) - $t0).TotalSeconds -gt 5) { break }
}
$state = (Get-ScheduledTask -TaskName $Task).State
Unregister-ScheduledTask -TaskName $Task -Confirm:$false
"user: $user, task: $state after $([math]::Round(((Get-Date) - $t0).TotalSeconds)) s"
