# One unattended run of the DOS 3dfx build in DOSBox Staging: New Game, no
# input, three raw captures. The config (see staging-3dfx.example.conf) decides
# which disc copy, and so which DREAMS.DAT and project, the game starts in.
param(
    [string]$Conf,
    [string]$Name,
    [string]$Work = (Join-Path $PSScriptRoot "..\..\..\..\out\recomp\dosbox"),
    [string]$DosBox = "E:\games\dosbox-staging\dosbox.exe"
)
$Work = (Resolve-Path $Work).Path
Stop-Process -Name dosbox -Force -ErrorAction SilentlyContinue; Start-Sleep 1
Start-Process $DosBox -ArgumentList '--conf', "$Work\$Conf" -WorkingDirectory (Split-Path $DosBox)
Start-Sleep 14
& "$PSScriptRoot\drive.ps1" -Work $Work -Steps "ESC,wait:3000,RETURN,wait:19000,snap:$Name-a,wait:4000,snap:$Name-b,wait:3000,snap:$Name-c"
Stop-Process -Name dosbox -Force -ErrorAction SilentlyContinue
