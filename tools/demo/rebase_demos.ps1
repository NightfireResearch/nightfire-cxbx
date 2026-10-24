# ---------------------------------------------------------------------------------------------------------------
# Rebases every time demo onto the current build (docs/action-timedemo.md, "Rebasing a recording onto changed
# code"): each recording's input is played into the game as it is now, at the game's own pace, and written out as
# a new recording beside it. The old files are left alone. Play the new ones back with tools/demo/play_demos.ps1
# -Demos "<out>\*.nfdemo", look at what changed, then copy them over the old ones.
#
#   tools/demo/rebase_demos.ps1                                 # tests\demos\action\*.nfdemo -> build\rebase\
#   tools/demo/rebase_demos.ps1 -Demos tests\demos\action\TowerA-*.nfdemo -Out build\rebase_towera
#
# A rebase takes as long as the session did (the sound comes from the device in real time).
# ---------------------------------------------------------------------------------------------------------------

param(
    [string]$Demos = "tests\demos\action\*.nfdemo",
    [string]$Out = "build\rebase",
    [string]$Exe = "Release\action.exe"
)

$repo = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $repo
$exePath = Resolve-Path $Exe
New-Item -ItemType Directory -Force $Out | Out-Null
$outDir = Resolve-Path $Out
$logDir = Join-Path $repo "build\demoplay"
New-Item -ItemType Directory -Force $logDir | Out-Null

Remove-Item Env:NIGHTFIRE_DEMO_RECORD, Env:NIGHTFIRE_DEMO_FAST, Env:NIGHTFIRE_DEMO_STOP, Env:NIGHTFIRE_ORIGINAL -ErrorAction SilentlyContinue
foreach ($demo in Get-ChildItem $Demos | Sort-Object Name) {
    $env:NIGHTFIRE_DEMO_PLAY = $demo.FullName
    $env:NIGHTFIRE_DEMO_REBASE = Join-Path $outDir $demo.Name
    $log = Join-Path $logDir "rebase_$($demo.BaseName).log"
    $proc = Start-Process -FilePath $exePath -WorkingDirectory (Split-Path $exePath) -RedirectStandardOutput $log -PassThru
    $proc.WaitForExit() | Out-Null
    $lines = Get-Content $log | Select-String "\[demo\] (rebased|frame \d+: the game parts)" | ForEach-Object { $_.Line -replace '^\[demo\] ', '' }
    Write-Output ("{0,-48} {1}" -f $demo.Name, ($lines -join '; '))
}
Remove-Item Env:NIGHTFIRE_DEMO_REBASE -ErrorAction SilentlyContinue
