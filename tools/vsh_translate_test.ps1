# Builds and runs tools/vsh_translate_test.cpp: the offline compile check for the D3D9 backend's vertex shader
# translator over all 130 of the game's NV2A shaders.
#
#   tools\vsh_translate_test.ps1 -Xbe Q:\path\to\default.xbe [-OutDir build\vsh]
#
# Dumps the shaders with tools/vsh_dump.py (needs `pip install nv2a-vsh`) if the output directory has none yet,
# then compiles the test with the newest 32-bit MSVC toolchain installed (found via its vcvars32.bat) and runs it.
param(
    [Parameter(Mandatory = $true)][string]$Xbe,
    [string]$OutDir = "build\vsh"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

if (-not (Test-Path (Join-Path $OutDir "vs000.bin"))) {
    python (Join-Path $PSScriptRoot "vsh_dump.py") $Xbe $OutDir | Out-Null
}

$candidates = @()
foreach ($base in @("${env:ProgramFiles}\Microsoft Visual Studio", "${env:ProgramFiles(x86)}\Microsoft Visual Studio")) {
    if (Test-Path $base) {
        $candidates += Get-ChildItem -Path $base -Recurse -Filter vcvars32.bat -ErrorAction SilentlyContinue
    }
}
$vcvars = $candidates | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $vcvars) { throw "No vcvars32.bat found under Program Files - install the MSVC x86 toolchain." }

$exe = Join-Path $OutDir "vsh_translate_test.exe"
$obj = Join-Path $OutDir "vsh_translate_test.obj"
$src = Join-Path $PSScriptRoot "vsh_translate_test.cpp"
$build = "call `"$($vcvars.FullName)`" >nul 2>&1 && cl.exe /nologo /EHsc /O1 /MD /Fo`"$obj`" /Fe`"$exe`" /I `"$root\src\action\engine\Direct3D`" /I `"$root\src\action\engine`" `"$src`" /link d3d9.lib d3dcompiler.lib winmm.lib user32.lib"
cmd /c $build
if ($LASTEXITCODE -ne 0) { throw "compile failed" }
& $exe $OutDir
exit $LASTEXITCODE
