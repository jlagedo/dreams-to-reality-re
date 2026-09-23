<#
.SYNOPSIS
  Persist Ghidra analysis into git. Run this often.

.DESCRIPTION
  A Ghidra project is gitignored, unmergeable and embeds the game executables,
  so it is not the record of what was learned — re/symbols/*.tsv is.

  This script exports named functions and comments from each program in the
  project into re/symbols/*.tsv, then optionally commits them. The TSV is the
  durable record; the Ghidra project is disposable.

  IMPORTANT: headless cannot open a project that the Ghidra GUI holds open — the
  project is locked. Two ways to checkpoint:

    GUI closed -> just run this script; it does both.
    GUI open   -> Ctrl+S, then Script Manager > Dreams > ExportSymbols.java,
                  then run this script with -SkipExport to commit the result.

.PARAMETER Message
  Commit message. Defaults to a timestamped one.

.PARAMETER SkipExport
  Do not run Ghidra; just stage and commit whatever is already in re/.
  Use this when the GUI is open and you exported from the Script Manager.

.PARAMETER NoCommit
  Export and show the diff, but do not commit.

.EXAMPLE
  .\tools\re-checkpoint.ps1 -Message "name the DSN header reader"
  .\tools\re-checkpoint.ps1 -SkipExport          # GUI is open
  .\tools\re-checkpoint.ps1 -NoCommit            # just look
#>
[CmdletBinding()]
param(
    [string]$Message,
    [switch]$SkipExport,
    [switch]$NoCommit,
    [string]$Ghidra,
    [string]$ProjectDir = "$PSScriptRoot\..\ghidra",
    [string]$ProjectName = "dreams",
    [string[]]$Programs = @("WINDREAM.EXE", "GDIDREAM.EXE", "SETUP.EXE", "CRYO.DLL")
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\dreams-env.ps1"
$repo = (Resolve-Path "$PSScriptRoot\..").Path
$env:DREAMS_REPO = $repo

if (-not $SkipExport) {
    if (-not $Ghidra) { $Ghidra = $env:DREAMS_GHIDRA_ROOT }
    if (-not $Ghidra) { $Ghidra = $env:GHIDRA_INSTALL_DIR }
    if (-not $Ghidra) { $Ghidra = Get-DreamsSetting DREAMS_GHIDRA_ROOT }
    if (-not $Ghidra) { $Ghidra = Get-DreamsSetting GHIDRA_INSTALL_DIR }
    if (-not $Ghidra) {
        throw "DREAMS_GHIDRA_ROOT is not configured. Copy dev/paths.example.env to .dreams.local.env."
    }
    $lock = Join-Path $ProjectDir "$ProjectName.lock"
    if (Test-Path $lock) {
        Write-Warning @"
The Ghidra project is LOCKED - the GUI has it open.
Headless cannot touch it. Do this instead:
  1. In Ghidra: Ctrl+S to save the program
  2. Window > Script Manager > Dreams > ExportSymbols.java
  3. Re-run:  .\tools\re-checkpoint.ps1 -SkipExport
"@
        exit 1
    }

    $headless = Join-Path $Ghidra "support\analyzeHeadless.bat"
    if (-not (Test-Path $headless)) { throw "analyzeHeadless not found at $headless" }

    foreach ($prog in $Programs) {
        Write-Host "exporting $prog..." -NoNewline
        $out = & $headless (Resolve-Path $ProjectDir).Path $ProjectName `
                   -process $prog -noanalysis -readOnly `
                   -scriptPath "$repo\ghidra_scripts" `
                   -postScript ExportSymbols.java 2>&1
        $line = $out | Where-Object { $_ -match 'named functions' } | Select-Object -First 1
        if ($line) {
            Write-Host (" " + ($line -replace '^INFO\s+ExportSymbols\.java>\s*', '' -replace '\s*\(GhidraScript\)\s*$', ''))
        } elseif ($out | Select-String -Quiet 'not found|No program') {
            Write-Host " (not in project, skipped)" -ForegroundColor DarkGray
        } else {
            Write-Host " no output - check the log" -ForegroundColor Yellow
        }
    }
}

Push-Location $repo
try {
    $changed = git status --porcelain -- re/ docs/ 2>$null
    if (-not $changed) {
        Write-Host "`nNothing changed since the last checkpoint." -ForegroundColor DarkGray
        return
    }

    Write-Host "`n--- changes ---" -ForegroundColor Cyan
    git --no-pager diff --stat -- re/ docs/
    git --no-pager diff --cached --stat -- re/ docs/

    # Guard: the whole point is that no game data ends up in git.
    $suspect = git status --porcelain | Where-Object {
        $_ -match '\.(rep|gpr|exe|dll|bin|iso|wav|hnm|dsn|dan|3dc|drd|tga|spr|bf|pak)\s*$'
    }
    if ($suspect) {
        Write-Warning "Refusing to commit - these look like game assets:`n$($suspect -join "`n")"
        exit 1
    }

    if ($NoCommit) {
        Write-Host "`n-NoCommit set; nothing committed." -ForegroundColor DarkGray
        return
    }

    if (-not $Message) {
        $Message = "re: checkpoint $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
    }
    git add re/ docs/
    git commit -q -m $Message
    Write-Host "`ncommitted: $Message" -ForegroundColor Green
    git --no-pager log --oneline -1
}
finally {
    Pop-Location
}
