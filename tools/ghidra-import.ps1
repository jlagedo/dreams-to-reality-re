<#
.SYNOPSIS
  Build the Ghidra project for Dreams to Reality from the disc images.

.DESCRIPTION
  The Ghidra project is a BUILD ARTEFACT. It is binary, unmergeable, and embeds
  copies of the game executables, so it never goes into git. This script
  regenerates it from your local discs, runs auto-analysis, and re-applies the
  symbol names stored in re/symbols/. Use -ImportStructs to parse the checked-in
  C layouts under re/structs/ into the game program's Data Type Manager.

  Run it once to set up, and again whenever you want a clean slate.

.PARAMETER Ghidra
  Ghidra installation directory.

.PARAMETER Project
  Where to create the Ghidra project. Gitignored by default.

.PARAMETER Binaries
  Which executables to import. Defaults to the Windows build (PE32, loads
  cleanly in Ghidra) plus CryoLib. The DOS builds are LE/DOS4GW and need the
  LE loader extension; see docs/re-setup.md, "LE loader for the DOS builds".
  Only the listed binaries are imported; other programs in the project stay.

.PARAMETER ImportStructs
  Parse re/structs/windream.h into the WINDREAM/GDI DREAM program Data Type Manager.

.EXAMPLE
  .\tools\ghidra-import.ps1
  .\tools\ghidra-import.ps1 -Analyze:$false   # import without auto-analysis
#>
[CmdletBinding()]
param(
    [string]$Ghidra,
    [string]$Project = "$PSScriptRoot\..\ghidra",
    [string]$ProjectName = "dreams",
    [string]$Disc1,
    [string]$Disc2,
    [string[]]$Binaries = @(),
    [switch]$Analyze = $true,
    [switch]$ImportSymbols,
    [switch]$ImportStructs
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\dreams-env.ps1"
if (-not $Ghidra) { $Ghidra = $env:DREAMS_GHIDRA_ROOT }
if (-not $Ghidra) { $Ghidra = $env:GHIDRA_INSTALL_DIR }
if (-not $Ghidra) { $Ghidra = Get-DreamsSetting DREAMS_GHIDRA_ROOT }
if (-not $Ghidra) { $Ghidra = Get-DreamsSetting GHIDRA_INSTALL_DIR }
if (-not $Ghidra) {
    throw "DREAMS_GHIDRA_ROOT is not configured. Copy dev/paths.example.env to .dreams.local.env."
}
$repo = Resolve-Path "$PSScriptRoot\.."
$headless = Join-Path $Ghidra "support\analyzeHeadless.bat"

if (-not (Test-Path $headless)) {
    throw "analyzeHeadless not found at $headless. Set -Ghidra or `$env:GHIDRA_INSTALL_DIR."
}

if ($Binaries.Count -eq 0) {
    if (-not $Disc1) { $Disc1 = Get-DreamsSetting DREAMS_DISC1 }
    if (-not $Disc2) { $Disc2 = Get-DreamsSetting DREAMS_DISC2 }
    if (-not $Disc1 -or -not $Disc2) {
        throw "DREAMS_DISC1 and DREAMS_DISC2 must be configured for the default import."
    }
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
    if ($ImportStructs)     { $args += @("-postScript", "ImportStructs.java") }

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
Write-Host "  4. Use -ImportStructs to load re/structs/windream.h into the game program"
