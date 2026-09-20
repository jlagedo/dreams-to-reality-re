<#
.SYNOPSIS
  Build the Ghidra project for Dreams to Reality from the disc images.

.DESCRIPTION
  The Ghidra project is a BUILD ARTEFACT. It is binary, unmergeable, and embeds
  copies of the game executables, so it never goes into git. This script
  regenerates it from your local discs, runs auto-analysis, and re-applies the
  symbol names stored in re/symbols/.

  Run it once to set up, and again whenever you want a clean slate.

.PARAMETER Ghidra
  Ghidra installation directory.

.PARAMETER Project
  Where to create the Ghidra project. Gitignored by default.

.PARAMETER Binaries
  Which executables to import. Defaults to the Windows build (PE32, loads
  cleanly in Ghidra) plus CryoLib. The DOS builds are LE/DOS4GW and need a
  loader extension - import them only if you have one.

.EXAMPLE
  .\tools\ghidra-import.ps1
  .\tools\ghidra-import.ps1 -Analyze:$false   # import without auto-analysis
#>
[CmdletBinding()]
param(
    [string]$Ghidra = $(if ($env:GHIDRA_INSTALL_DIR) { $env:GHIDRA_INSTALL_DIR } else { "E:\tools\ghidra_12.1.3_PUBLIC" }),
    [string]$Project = "$PSScriptRoot\..\ghidra",
    [string]$ProjectName = "dreams",
    [string]$Disc1 = $(if ($env:DREAMS_DISC1) { $env:DREAMS_DISC1 } else { "E:\dev_game\Dreams-to-Reality_Win_EN_Disc-Image-Disk-1\extracted" }),
    [string]$Disc2 = $(if ($env:DREAMS_DISC2) { $env:DREAMS_DISC2 } else { "E:\dev_game\Dreams-to-Reality_Win_EN_Disc-Image-Disk-2\extracted" }),
    [string[]]$Binaries = @(),
    [switch]$Analyze = $true,
    [switch]$ImportSymbols
)

$ErrorActionPreference = "Stop"
$repo = Resolve-Path "$PSScriptRoot\.."
$headless = Join-Path $Ghidra "support\analyzeHeadless.bat"

if (-not (Test-Path $headless)) {
    throw "analyzeHeadless not found at $headless. Set -Ghidra or `$env:GHIDRA_INSTALL_DIR."
}

if ($Binaries.Count -eq 0) {
    $Binaries = @(
        (Join-Path $Disc1 "WINDREAM.EXE"),   # PE32 Watcom - the primary target
        (Join-Path $Disc1 "GDIDREAM.EXE"),   # same build, GDI backend
        (Join-Path $Disc1 "SETUP.EXE"),      # MSVC installer, reads SETUP.INI
        (Join-Path $Disc2 "DEMOS2\CRYO.DLL") # CryoLib debug build, 165 exports
    )
}

$missing = $Binaries | Where-Object { -not (Test-Path $_) }
if ($missing) {
    Write-Warning "Skipping missing binaries:`n  $($missing -join "`n  ")"
    $Binaries = $Binaries | Where-Object { Test-Path $_ }
}
if ($Binaries.Count -eq 0) { throw "No binaries to import. Check `$env:DREAMS_DISC1." }

New-Item -ItemType Directory -Force -Path $Project | Out-Null
$Project = (Resolve-Path $Project).Path

Write-Host "Ghidra   : $Ghidra"
Write-Host "Project  : $Project\$ProjectName"
Write-Host "Scripts  : $repo\ghidra_scripts"
Write-Host "Importing: $($Binaries.Count) binaries`n"

$env:DREAMS_REPO = $repo

foreach ($bin in $Binaries) {
    Write-Host "--- $(Split-Path $bin -Leaf) ---" -ForegroundColor Cyan
    $args = @(
        $Project, $ProjectName,
        "-import", $bin,
        "-overwrite",
        "-scriptPath", "$repo\ghidra_scripts"
    )
    if (-not $Analyze)      { $args += "-noanalysis" }
    if ($ImportSymbols)     { $args += @("-postScript", "ImportSymbols.java") }

    & $headless @args 2>&1 | Where-Object {
        $_ -notmatch 'INFO|WARN\s+Unable to|^\s*$' -or $_ -match 'ERROR|Exception'
    } | Select-Object -Last 8
}

Write-Host "`nDone. Open the project:" -ForegroundColor Green
Write-Host "  $Ghidra\ghidraRun.bat    then File > Open Project > $Project\$ProjectName.gpr"
Write-Host "`nNext:"
Write-Host "  1. Window > Script Manager > Dreams > FindFormatParsers.java"
Write-Host "  2. Rename what you identify"
Write-Host "  3. Script Manager > ExportSymbols.java  (persists names to re/symbols/)"
