# ---------------------------------------------------------------------------------------------------------------
# Plays one time demo with a set of ported functions put back to the original (NIGHTFIRE_ORIGINAL), stopping at
# the first frame that differs, and prints the result line. The step of a bisection: when a parcel of ports makes
# a demo differ, play it with half the parcel's addresses restored, then half again, until one function is left.
#
#   tools/demo/bisect_originals.ps1 -Demo tests\demos\action\CastleCourtyard-*.nfdemo -Originals 0x47a10,0x47b40
#   tools/demo/bisect_originals.ps1 -Demo ... -OriginalsFile build\some_list.txt     # comma-separated addresses
#
# docs/action-timedemo.md says what NIGHTFIRE_ORIGINAL can and cannot put back: a port that calls another port
# directly by its C++ name does not go through the original's address, so restoring the callee does not reach it.
# ---------------------------------------------------------------------------------------------------------------

param(
    [Parameter(Mandatory = $true)][string]$Demo,
    [string]$Originals = "",
    [string]$OriginalsFile = "",
    [string]$Exe = "Release\action.exe",
    [string]$Log = "build\demoplay\bisect.log"
)

$repo = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $repo
$exePath = Resolve-Path $Exe
$demoPath = (Get-ChildItem $Demo | Select-Object -First 1).FullName
if ($OriginalsFile -ne "") { $Originals = (Get-Content $OriginalsFile -Raw).Trim() }

Remove-Item Env:NIGHTFIRE_DEMO_RECORD, Env:NIGHTFIRE_DEMO_DUMP_GAMESTATE -ErrorAction SilentlyContinue
$env:NIGHTFIRE_DEMO_PLAY = $demoPath
$env:NIGHTFIRE_DEMO_FAST = "4"
$env:NIGHTFIRE_DEMO_STOP = "1"
if ($Originals -ne "") { $env:NIGHTFIRE_ORIGINAL = $Originals } else { Remove-Item Env:NIGHTFIRE_ORIGINAL -ErrorAction SilentlyContinue }

New-Item -ItemType Directory -Force (Split-Path $Log) | Out-Null
$proc = Start-Process -FilePath $exePath -WorkingDirectory (Split-Path $exePath) -RedirectStandardOutput $Log -PassThru
$proc.WaitForExit(1800000) | Out-Null
Get-Content $Log | Select-String "\[demo\] (playback|NIGHTFIRE_ORIGINAL|frame \d+ differs)" | Select-Object -First 3 | ForEach-Object { $_.Line }
