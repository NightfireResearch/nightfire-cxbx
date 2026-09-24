# Builds and runs tools/xadpcm_test.cpp: the offline check for the audio backend's Xbox ADPCM decoder
# (src/action/sound/xadpcm.cpp). Needs nothing from the game.
#
#   tools\xadpcm_test.ps1 [-OutDir build\xadpcm]
#   tools\xadpcm_test.ps1 -In some.adpcm -Out some.wav [-Channels 1]
#
# With -In/-Out it decodes a real ADPCM blob to a playable .wav instead of running the self-checks.
param(
    [string]$OutDir = "build\xadpcm",
    [string]$In,
    [string]$Out,
    [int]$Channels = 1
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

$candidates = @()
foreach ($base in @("${env:ProgramFiles}\Microsoft Visual Studio", "${env:ProgramFiles(x86)}\Microsoft Visual Studio")) {
    if (Test-Path $base) {
        $candidates += Get-ChildItem -Path $base -Recurse -Filter vcvars32.bat -ErrorAction SilentlyContinue
    }
}
$vcvars = $candidates | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $vcvars) { throw "No vcvars32.bat found under Program Files - install the MSVC x86 toolchain." }

$exe = Join-Path $OutDir "xadpcm_test.exe"
$obj = Join-Path $OutDir "xadpcm_test.obj"
$src = Join-Path $PSScriptRoot "xadpcm_test.cpp"
$build = "call `"$($vcvars.FullName)`" >nul 2>&1 && cl.exe /nologo /EHsc /O2 /MD /W3 /D_CRT_SECURE_NO_WARNINGS /Fo`"$obj`" /Fe`"$exe`" `"$src`""
cmd /c $build
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

if ($In) {
    if (-not $Out) { throw "-In needs -Out" }
    & $exe $In $Out $Channels
} else {
    & $exe
}
exit $LASTEXITCODE
