# Link one trivial NT program with each wlink and print the PE linker version it stamps.
$ErrorActionPreference = 'Stop'
$w = Join-Path $env:TEMP 'linkver'; New-Item -ItemType Directory -Force $w | Out-Null
Set-Content "$w\t.c" 'int main(void) { return 0; }'
$kits = [ordered]@{
    '10.6 GA' = @('E:\dev_game\watcom\wc106\watcom10.6', 'E:\dev_game\watcom\wc106\watcom10.6\BINNT\WLINK.EXE')
    '10.6a'   = @('E:\dev_game\watcom\wc106\watcom10.6', 'E:\dev_game\watcom\wc106a\10.6a\BINNT\WLINK.EXE')
    '11.0'    = @('E:\dev_game\watcom\wc110\11.0', 'E:\dev_game\watcom\wc110\11.0\BINNT\WLINK.EXE')
}
foreach ($k in $kits.Keys) {
    $root, $link = $kits[$k]
    $env:WATCOM = $root; $env:LIB = "$root\LIB386;$root\LIB386\NT"; $env:INCLUDE = "$root\H;$root\H\NT"
    $env:PATH = "$root\BINNT;$root\BINW;" + $env:PATH
    Push-Location $w
    & "$root\BINNT\WCC386.EXE" -zq -bt=nt t.c | Out-Null
    & $link option quiet system nt file t.obj name t.exe | Out-Null
    $b = [IO.File]::ReadAllBytes("$w\t.exe"); $pe = [BitConverter]::ToInt32($b, 0x3c)
    "{0,-8} linker {1}.{2}" -f $k, $b[$pe + 24 + 2], $b[$pe + 24 + 3]
    Copy-Item t.exe (Join-Path $w (($k -replace ' ', '_') + '.exe')); Remove-Item t.exe, t.obj; Pop-Location
}
