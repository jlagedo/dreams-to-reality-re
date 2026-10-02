# One unattended run of the DOS 3dfx build in DOSBox-X. DOSBox-X ignores posted
# keys, so the config's AUTOTYPE line presses ESC at 12 s and ENTER at 15 s
# (see dosbox-x-3dfx.example.conf); this only waits and grabs the window.
param(
    [string]$Conf,
    [string]$Name,
    [string]$Steps = "",
    [string]$Work = (Join-Path $PSScriptRoot "..\..\..\..\out\recomp\dosbox"),
    [string]$DosBox = "C:\DOSBox-X\dosbox-x.exe"
)
$Work = (Resolve-Path $Work).Path
Stop-Process -Name dosbox-x -Force -ErrorAction SilentlyContinue; Start-Sleep 1
Start-Process $DosBox -ArgumentList '-conf', "$Work\$Conf", '-fastlaunch' -WorkingDirectory $Work
if (-not $Steps) { $Steps = "wait:41000,grab:$Name-a,wait:3000,grab:$Name-b,wait:3000,grab:$Name-c" }
Start-Sleep 2
& "$PSScriptRoot\drive-x.ps1" -Work $Work -Steps $Steps
Stop-Process -Name dosbox-x -Force -ErrorAction SilentlyContinue
