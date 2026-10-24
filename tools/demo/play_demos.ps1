# ---------------------------------------------------------------------------------------------------------------
# Plays action engine time demos back and says which still match their recording frame for frame
# (docs/action-timedemo.md). The test for a change to the action engine: every recording should still match.
#
#   tools/demo/play_demos.ps1                                  # every recording in tests\demos\action, unpaced
#   tools/demo/play_demos.ps1 -Demos tests\demos\action\TowerA-*.nfdemo
#   tools/demo/play_demos.ps1 -Fast 0                          # at the game's own speed, to watch
#   tools/demo/play_demos.ps1 -Stop                            # each stops at its first frame that differs
#
# Each runs Release\action.exe with NIGHTFIRE_DEMO_PLAY from Release\ (the playback's sandbox is
# Release\demoplay\<name>); its log goes to build\demoplay\<name>.log. Exits 1 if any recording failed to match.
#
# The 12 recordings play on AMD and Intel alike (the x87's transcendentals differ between them only where rounding to
# a float hides it): tests\demos\action\README.md and docs\action-timedemo.md, "Limits".
# ---------------------------------------------------------------------------------------------------------------

param(
    [string]$Demos = "tests\demos\action\*.nfdemo",
    [int]$Fast = 4,
    [switch]$Stop,
    [string]$Exe = "Release\action.exe",
    [int]$TimeoutMinutes = 30
)

$repo = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $repo
$exePath = Resolve-Path $Exe
$workDir = Split-Path $exePath
$logDir = Join-Path $repo "build\demoplay"
New-Item -ItemType Directory -Force $logDir | Out-Null

Remove-Item Env:NIGHTFIRE_DEMO_RECORD -ErrorAction SilentlyContinue
if ($Fast -gt 0) { $env:NIGHTFIRE_DEMO_FAST = "$Fast" } else { Remove-Item Env:NIGHTFIRE_DEMO_FAST -ErrorAction SilentlyContinue }
if ($Stop) { $env:NIGHTFIRE_DEMO_STOP = "1" } else { Remove-Item Env:NIGHTFIRE_DEMO_STOP -ErrorAction SilentlyContinue }

$failed = 0
$files = Get-ChildItem $Demos | Sort-Object Name
foreach ($demo in $files) {
    $env:NIGHTFIRE_DEMO_PLAY = $demo.FullName
    $log = Join-Path $logDir "$($demo.BaseName).log"
    $proc = Start-Process -FilePath $exePath -WorkingDirectory $workDir -RedirectStandardOutput $log -PassThru
    if (-not $proc.WaitForExit($TimeoutMinutes * 60000)) {
        $proc.Kill()
        $failed++
        Write-Output ("{0,-48} TIMED OUT after {1} minutes" -f $demo.Name, $TimeoutMinutes)
        continue
    }
    $result = Get-Content $log | Select-String "\[demo\] playback" | Select-Object -Last 1
    $line = if ($result) { ($result.Line -replace '^\[demo\] playback ', '') } else { "no result (see $log)" }
    if (-not ($line -match "every frame matched")) { $failed++ }
    Write-Output ("{0,-48} {1}" -f $demo.Name, $line)
}
Write-Output ""
Write-Output "$($files.Count - $failed) of $($files.Count) matched"
exit ([int]($failed -ne 0))
