# Drive a running DOSBox-X window without focus (see drive.ps1).
# Steps: key name (tap), wait:<ms>, hold:<KEY>:<ms>,
#   snap:<name>  DOSBox-X's own screenshot (host key F11 + P), copied to OutDir
#   grab:<name>  PrintWindow of the client area (what the host window shows)
param(
    [string]$Steps = "grab:now",
    [string]$Work = (Join-Path $PSScriptRoot "..\..\..\..\out\recomp\dosbox"),
    [string]$OutDir = (Join-Path $Work "shots"),
    [string]$CaptureDir = (Join-Path $Work "capture-x")
)
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class DrvX {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint type);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
    public static void Cmd(IntPtr h, int id) { PostMessage(h, 0x111u, (IntPtr)id, IntPtr.Zero); }
    public static void Init() { SetProcessDPIAware(); }
    public static int[] Size(IntPtr h) { RECT r; GetClientRect(h, out r); return new int[] { r.r - r.l, r.b - r.t }; }
    public static void Key(IntPtr h, uint scan, bool ext, bool up) {
        uint vk = MapVirtualKey(scan | (ext ? 0xE000u : 0u), 3);
        uint l = 1u | (scan << 16) | (ext ? 1u << 24 : 0u) | (up ? 0xC0000000u : 0u);
        PostMessage(h, up ? 0x101u : 0x100u, (IntPtr)vk, (IntPtr)unchecked((int)l));
    }
}
'@
[DrvX]::Init()
$scan = @{ ESC = 0x01; RETURN = 0x1C; SPACE = 0x39; P = 0x19; F11 = 0x57; F12 = 0x58; CTRL = 0x1D; F5 = 0x3F }
$ext = @{ UP = 0x48; DOWN = 0x50; LEFT = 0x4B; RIGHT = 0x4D }

$p = Get-Process dosbox-x -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Output "no dosbox-x window"; exit 1 }
$h = $p.MainWindowHandle

function Code($name) {
    if ($ext.ContainsKey($name)) { return @($ext[$name], $true) }
    if ($scan.ContainsKey($name)) { return @($scan[$name], $false) }
    throw "unknown key $name"
}
function Tap($name, $ms = 80) {
    $c = Code $name
    [DrvX]::Key($h, $c[0], $c[1], $false); Start-Sleep -Milliseconds $ms; [DrvX]::Key($h, $c[0], $c[1], $true)
}
function Snap($name) {
    $before = @(Get-ChildItem $CaptureDir -Filter *.png -ErrorAction SilentlyContinue).Count
    [DrvX]::Cmd($h, 4894)
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 100
        $files = @(Get-ChildItem $CaptureDir -Filter *.png -ErrorAction SilentlyContinue | Sort-Object LastWriteTime)
        if ($files.Count -gt $before) {
            Start-Sleep -Milliseconds 200
            $dst = Join-Path $OutDir ($name + '.png'); Copy-Item $files[-1].FullName $dst -Force
            return "snap $dst <- $($files[-1].Name)"
        }
    }
    "snap ${name}: no capture written"
}
function Grab($name) {
    $s = [DrvX]::Size($h)
    $b = New-Object System.Drawing.Bitmap $s[0], $s[1]
    $g = [System.Drawing.Graphics]::FromImage($b); $dc = $g.GetHdc()
    $ok = [DrvX]::PrintWindow($h, $dc, 3)
    $g.ReleaseHdc($dc); $g.Dispose()
    $dst = Join-Path $OutDir ($name + '.png'); $b.Save($dst, [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
    "grab $dst $($s[0])x$($s[1]) ok=$ok"
}

New-Item -ItemType Directory -Force $OutDir, $CaptureDir | Out-Null
foreach ($step in $Steps.Split(',')) {
    $parts = $step.Trim().Split(':')
    switch ($parts[0]) {
        'wait' { Start-Sleep -Milliseconds ([int]$parts[1]) }
        'snap' { Write-Output (Snap $parts[1]) }
        'grab' { Write-Output (Grab $parts[1]) }
        'hold' { Tap $parts[1] ([int]$parts[2]) }
        default { Tap $parts[0]; Start-Sleep -Milliseconds 150 }
    }
}
$p.Refresh(); Write-Output $p.MainWindowTitle
