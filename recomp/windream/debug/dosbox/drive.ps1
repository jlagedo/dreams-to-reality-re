# Drive a running DOSBox window without focus: keys are posted to its window
# (SDL reads the scancode from the message), frames come from DOSBox's own
# Ctrl+F5 capture, so the desktop can be locked or in use.
#   drive.ps1 -Steps "ESC,wait:2000,RETURN,snap:menu"
# Steps: a key name (tap), wait:<ms>, hold:<KEY>:<ms>, snap:<name> (capture,
# copied to <OutDir>/<name>.png).
param(
    [string]$Steps = "snap:now",
    [string]$Work = (Join-Path $PSScriptRoot "..\..\..\..\out\recomp\dosbox"),
    [string]$OutDir = (Join-Path $Work "shots"),
    [string]$CaptureDir = (Join-Path $Work "capture")
)

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Drv {
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint type);
    public static void Key(IntPtr h, uint scan, bool ext, bool up) {
        uint vk = MapVirtualKey(scan | (ext ? 0xE000u : 0u), 3);
        uint l = 1u | (scan << 16) | (ext ? 1u << 24 : 0u) | (up ? 0xC0000000u : 0u);
        PostMessage(h, up ? 0x101u : 0x100u, (IntPtr)vk, (IntPtr)unchecked((int)l));
    }
}
'@

$scan = @{
    ESC = 0x01; RETURN = 0x1C; SPACE = 0x39; TAB = 0x0F; CTRL = 0x1D; ALT = 0x38; SHIFT = 0x2A
    F1 = 0x3B; F2 = 0x3C; F3 = 0x3D; F4 = 0x3E; F5 = 0x3F; F6 = 0x40; F9 = 0x43; F10 = 0x44
    Y = 0x15; N = 0x31; A = 0x1E; S = 0x1F; D = 0x20; W = 0x11; Q = 0x10; X = 0x2D; Z = 0x2C; C = 0x2E
    '1' = 0x02; '2' = 0x03; '3' = 0x04
}
$ext = @{ UP = 0x48; DOWN = 0x50; LEFT = 0x4B; RIGHT = 0x4D }

$p = Get-Process dosbox -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Output "no dosbox window"; exit 1 }
$h = $p.MainWindowHandle

function Tap($name, $ms = 80) {
    if ($ext.ContainsKey($name)) { $s = $ext[$name]; $e = $true }
    elseif ($scan.ContainsKey($name)) { $s = $scan[$name]; $e = $false }
    else { throw "unknown key $name" }
    [Drv]::Key($h, $s, $e, $false); Start-Sleep -Milliseconds $ms; [Drv]::Key($h, $s, $e, $true)
}

function Snap($name) {
    $before = @(Get-ChildItem $CaptureDir -Filter *.png -ErrorAction SilentlyContinue).Count
    [Drv]::Key($h, 0x1D, $false, $false); Start-Sleep -Milliseconds 60
    Tap 'F5'
    Start-Sleep -Milliseconds 60; [Drv]::Key($h, 0x1D, $false, $true)
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 100
        $files = @(Get-ChildItem $CaptureDir -Filter *.png -ErrorAction SilentlyContinue | Sort-Object LastWriteTime)
        if ($files.Count -gt $before) {
            Start-Sleep -Milliseconds 200
            $dst = Join-Path $OutDir ($name + '.png')
            Copy-Item $files[-1].FullName $dst -Force
            return "snap $dst <- $($files[-1].Name)"
        }
    }
    "snap ${name}: no capture written"
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
foreach ($step in $Steps.Split(',')) {
    $parts = $step.Trim().Split(':')
    switch ($parts[0]) {
        'wait' { Start-Sleep -Milliseconds ([int]$parts[1]) }
        'snap' { Write-Output (Snap $parts[1]) }
        'hold' { Tap $parts[1] ([int]$parts[2]) }
        default { Tap $parts[0]; Start-Sleep -Milliseconds 150 }
    }
}
Write-Output $p.MainWindowTitle
