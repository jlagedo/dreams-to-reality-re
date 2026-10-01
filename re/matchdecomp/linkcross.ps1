# Cross linker x runtime libraries to see which one decides section layout and flags.
$ErrorActionPreference = 'Stop'
$w = Join-Path $PSScriptRoot 'linkver'
Set-Content "$w\t.c" 'int main(void) { return 0; }'
$r106 = 'E:\dev_game\watcom\wc106\watcom10.6'; $r110 = 'E:\dev_game\watcom\wc110\11.0'
$path0 = $env:PATH
$combos = @(
    @('link10.6+lib10.6', $r106, $r106), @('link10.6+lib11.0', $r106, $r110),
    @('link11.0+lib10.6', $r110, $r106), @('link11.0+lib11.0', $r110, $r110))
foreach ($c in $combos) {
    $name, $lroot, $libroot = $c
    $env:PATH = "$lroot\BINNT;$lroot\BINW;$path0"
    $env:WATCOM = $lroot; $env:LIB = "$libroot\LIB386;$libroot\LIB386\NT"; $env:INCLUDE = "$libroot\H;$libroot\H\NT"
    Push-Location $w
    & "$libroot\BINNT\WCC386.EXE" -zq -bt=nt t.c | Out-Null
    $p = Start-Process "$lroot\BINNT\WLINK.EXE" -ArgumentList "option quiet system nt file t.obj name $name.exe" -NoNewWindow -PassThru -RedirectStandardInput NUL
    if (-not $p.WaitForExit(30000)) { $p.Kill(); "$name : wlink hung" }
    Pop-Location
}
