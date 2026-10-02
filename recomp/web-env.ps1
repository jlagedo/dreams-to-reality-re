# Activate the machine-local browser tools without changing the system PATH.
# Run from the repository root: . ./recomp/web-env.ps1
param([string]$Config = $env:DREAMS_WEB_TOOLS)

$webRepo = Split-Path -Parent $PSScriptRoot
if (-not $Config) {
    $Config = Join-Path $webRepo 'out/recomp/web-tools/tools.json'
}
if (-not (Test-Path -LiteralPath $Config)) {
    throw "Browser tools configuration is missing: $Config"
}
$webTools = Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
foreach ($webToolPath in @($webTools.emsdk, $webTools.cmake, $webTools.ninja,
        $webTools.python, $webTools.shdc, $webTools.sdl3)) {
    if (-not (Test-Path -LiteralPath $webToolPath)) {
        throw "Browser tool or dependency is missing: $webToolPath"
    }
}

$webQuietBefore = $env:EMSDK_QUIET
$env:EMSDK_QUIET = '1'
try {
    & (Join-Path $webTools.emsdk 'emsdk_env.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Emscripten environment activation failed.' }
} finally {
    $env:EMSDK_QUIET = $webQuietBefore
}

$webPathDirs = @(
    (Split-Path -Parent $webTools.cmake)
    (Split-Path -Parent $webTools.ninja)
    (Split-Path -Parent $webTools.python)
    (Split-Path -Parent $env:EMSDK_NODE)
    (Split-Path -Parent $webTools.shdc)
)
$env:PATH = (($webPathDirs + ($env:PATH -split ';')) | Select-Object -Unique) -join ';'
$env:DREAMS_EMSDK = $webTools.emsdk
$env:DREAMS_WEB_SDL3 = $webTools.sdl3
$env:SDL3_DIR = Join-Path $webTools.sdl3 'lib/cmake/SDL3'
$env:DREAMS_SHDC = $webTools.shdc
$env:DREAMS_WEB_TOOLS = (Resolve-Path -LiteralPath $Config).Path
$webPrefixes = @($webTools.sdl3) + @($env:CMAKE_PREFIX_PATH -split ';' | Where-Object { $_ })
$env:CMAKE_PREFIX_PATH = ($webPrefixes | Select-Object -Unique) -join ';'
Write-Host "Browser tools ready: Emscripten $($webTools.emscripten_version), SDL3 (pthreads)."
