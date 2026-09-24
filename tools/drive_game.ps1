# ---------------------------------------------------------------------------------------------------------------
# Launches the game and presses keys at it, so that a fault which only happens several menus in can be
# reproduced without a person at the keyboard.
#
# This exists because most of the interesting failures are not at startup. Both of the crashes found after the
# standalone loader first booted - starting a mission, and opening the codename screen - needed someone to
# navigate there, and each fix then needed the same navigation again to check. An agent working on this
# repository cannot do that by hand, and guessing at a cause instead of reproducing it is how this project
# loses test cycles.
#
# Keys go in with keybd_event rather than as posted window messages, because the game reads input with
# GetAsyncKeyState - it asks the system what is physically held down, and a posted WM_KEYDOWN does not change
# that. The window also has to be in the foreground first, or psiInput's focus check ignores the keys.
#
# USAGE (from the repository root, after building the action and actioninject targets):
#
#   tools/drive_game.ps1 -Keys enter,enter,enter
#   tools/drive_game.ps1 -Keys enter,down,down,enter -BetweenKeysMs 2500 -TailWaitMs 30000
#
# Everything the game prints goes to the log file named at the end; the loader's fault reporter prints the
# faulting address, what it touched, and a call stack, which is usually enough to name the cause in Ghidra
# without attaching a debugger.
#
# GETTING INTO A LEVEL, which is what most of this is for: three "enter" presses, a few seconds apart, gets
# from the attract movie through the codename screen to a loaded mission. Note that arriving in a level is not
# the same as being in control of it - the opening in-engine cutscene runs first, during which the player
# object already exists and input mostly does not apply. "enter" skips the cutscene too (it is the A button);
# "escape" would also skip it, but a second press pauses the game instead.
#
# THE DRIVING ENGINE maps the same keys differently (src/driving/platform/XboxInput.cpp): "enter" is START,
# "space" is A, "escape" is BACK. Pass -Exe Release\driving.exe to drive it; "enter" is what skips its intro
# movie and answers its "press START" prompts.
#
# KEY NAMES are the host keyboard's, and the game's mapping of them is in src/action/engine/psiInput.cpp
# (BuildKeyboardPadState). The ones worth knowing: "enter" is A, "back" is B, "escape" is Start, and the
# arrow keys are the right stick. Any single character - "c", "p", "1" - is sent as that key.
# ---------------------------------------------------------------------------------------------------------------

param(
    # Keys to press, in order, once the game has started.
    [string[]]$Keys = @(),

    # How long to let the game get to its first menu before pressing anything. The default is generous
    # because a cold run reads a lot off disc.
    [int]$StartupWaitMs = 16000,

    # Gap between key presses. Menus animate, and pressing again too early is swallowed.
    [int]$BetweenKeysMs = 2000,

    # How long to keep running after the last key, which is when a fault usually arrives.
    [int]$TailWaitMs = 30000,

    # A key to hold down for HoldMs after the last key press - "w" drives the car forward - so that a run
    # can reach the things that only happen in motion. Empty holds nothing.
    [string]$HoldKey = "",
    [int]$HoldMs = 0,
    [int]$HoldDelayMs = 0,   # how long after the last key press the hold starts (a level load, say)

    # Or, instead of a fixed delay: start the hold once the game's log matches this pattern, polling it
    # until HoldDelayMs runs out. "draw mix: [1-9][0-9][0-9]? indexed" is the driving engine drawing a level
    # (its PerfLog line), which is what makes "get into the level, then press START" reproducible when the
    # load takes a different time on every run.
    [string]$HoldAfterPattern = "",

    # Keys to press after the hold, the same way as Keys - "f8" to record where a drive ended up, say.
    [string[]]$AfterHoldKeys = @(),

    # Driving engine only: put the player's car at "x,y,z,dx,dy,dz" once it has been in the level for
    # TeleportDelayMs (settings.ini, default 3 s), then dump that frame TeleportDumpMs later (default 2 s). The
    # places come from pressing F8 in-game, which prints them and appends them to Release/teleports.txt. Passed
    # to the game as NIGHTFIRE_TELEPORT; see src/driving/devtools/Teleport.cpp.
    [string]$Teleport = "",

    # Driving engine only, and both independent of window focus and of any real pad: hold "brake" or
    # "accelerate" from inside the game for the whole run (NIGHTFIRE_HOLD), and/or dump one frame this many ms
    # after the car appears without teleporting it (NIGHTFIRE_DUMP_MS). See src/driving/platform/XboxInput.cpp.
    [string]$GameHold = "",
    [int]$DumpAfterMs = -1,

    # End the run as soon as the game's log matches this, instead of waiting out TailWaitMs. With -Teleport,
    # "teleport\] dumping frame" stops once the frame is asked for (plus a second for it to be written).
    [string]$StopPattern = "",

    # Arguments for the game's executable. For the driving engine, "-mission 6" (or "-mission snow2a_mis4")
    # starts that mission - or part of one - with no psiLaunch.bin juggling; see
    # src/driving/platform/LaunchOptions.cpp for the list and the other options.
    [string]$GameArgs = "",

    [string]$Exe = "Release\action.exe",
    [string]$WorkingDirectory = "Release",
    [string]$LogPath = "$env:TEMP\nightfire-drive.log"
)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class DriveGameNative {
    [DllImport("user32.dll")] public static extern IntPtr FindWindowA(string cls, string name);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
}
'@

$exePath = (Resolve-Path $Exe -ErrorAction Stop).Path
$workDir = (Resolve-Path $WorkingDirectory -ErrorAction Stop).Path
Remove-Item $LogPath -ErrorAction SilentlyContinue

# Inherited by the game; cleared again below so that it does not outlive this run in the calling shell.
if ($Teleport -ne "") { $env:NIGHTFIRE_TELEPORT = $Teleport } else { Remove-Item Env:NIGHTFIRE_TELEPORT -ErrorAction SilentlyContinue }
if ($GameHold -ne "") { $env:NIGHTFIRE_HOLD = $GameHold } else { Remove-Item Env:NIGHTFIRE_HOLD -ErrorAction SilentlyContinue }
if ($DumpAfterMs -ge 0) { $env:NIGHTFIRE_DUMP_MS = "$DumpAfterMs" } else { Remove-Item Env:NIGHTFIRE_DUMP_MS -ErrorAction SilentlyContinue }
$startArgs = @{ FilePath = $exePath; WorkingDirectory = $workDir; PassThru = $true
                RedirectStandardOutput = $LogPath; RedirectStandardError = "$LogPath.err" }
if ($GameArgs -ne "") { $startArgs.ArgumentList = $GameArgs }
$proc = Start-Process @startArgs
Remove-Item Env:NIGHTFIRE_TELEPORT, Env:NIGHTFIRE_HOLD, Env:NIGHTFIRE_DUMP_MS -ErrorAction SilentlyContinue

Start-Sleep -Milliseconds $StartupWaitMs

if ($proc.HasExited) {
    Write-Output "!! the game exited during startup (code $($proc.ExitCode)) - see $LogPath"
    Get-Content $LogPath -ErrorAction SilentlyContinue | Select-Object -Last 25
    exit 1
}

# The process's own main window is more reliable than looking the class up by name, which depends on agreeing
# with how the name is marshalled. The class name is the fallback; the loader registers it, and the injected
# DLL finds the window the same way (see src/common/renderWindow.h).
$proc.Refresh()
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { $hwnd = [DriveGameNative]::FindWindowA("NightfireRender", $null) }

# Windows only lets the process that owns the foreground give it away, and a script started from a tool
# shell owns nothing. Tapping ALT first is the documented way round that: the shell then counts as having
# had recent input, and SetForegroundWindow is allowed. Called before every key and before the hold, since
# the game's own window can lose the foreground again between them.
function Focus-Game([IntPtr]$h) {
    if ([DriveGameNative]::GetForegroundWindow() -eq $h) { return $true }
    [void][DriveGameNative]::ShowWindow($h, 5)
    [DriveGameNative]::keybd_event(0x12, 0, 0, [IntPtr]::Zero)      # ALT down
    [DriveGameNative]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)      # ALT up
    [void][DriveGameNative]::SetForegroundWindow($h)
    Start-Sleep -Milliseconds 300
    return ([DriveGameNative]::GetForegroundWindow() -eq $h)
}

# Presses each key in turn, BetweenKeysMs apart.
# Called as "powershell -File drive_game.ps1 -Keys enter,enter" - which is how anything other than a
# PowerShell prompt has to call it - the whole list arrives as one string, because -File does not parse
# arguments the way the shell does. Splitting here makes both spellings work.
function Press-Keys([string[]]$list) {
    $keyList = @($list | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne '' })
    foreach ($key in $keyList) {
        $key = $key.Trim()
        if ($proc.HasExited) { Write-Output "!! exited before key '$key'"; break }

        $vk = switch ($key) {
            "enter"  { 0x0D }   # A
            "back"   { 0x08 }   # B
            "escape" { 0x1B }   # Start
            "up"     { 0x26 }
            "down"   { 0x28 }
            "left"   { 0x25 }
            "right"  { 0x27 }
            "space"  { 0x20 }
            "f8"     { 0x77 }   # driving engine: record the car's place (src/driving/devtools/Teleport.cpp)
            "f9"     { 0x78 }   # driving engine: teleport back to it
            default  {
                if ($key.Length -ne 1) { throw "unknown key '$key' - see the key names in this script's header" }
                [byte][char]$key.ToUpper()
            }
        }

        Write-Output ">> $key"
        [void](Focus-Game $hwnd)
        [DriveGameNative]::keybd_event([byte]$vk, 0, 0, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 120
        [DriveGameNative]::keybd_event([byte]$vk, 0, 2, [IntPtr]::Zero)   # 2 = KEYEVENTF_KEYUP
        Start-Sleep -Milliseconds $BetweenKeysMs
    }
}

if ($hwnd -eq [IntPtr]::Zero) {
    Write-Output "!! no game window found - keys would go to whatever is in front, so not sending any"
} else {
    if (-not (Focus-Game $hwnd)) {
        Write-Output "!! could not bring the game to the foreground; its focus check will ignore the keys"
    }

    Press-Keys $Keys
}

if ($HoldKey -ne "" -and $HoldMs -gt 0 -and -not $proc.HasExited) {
    $hold = if ($HoldKey.Length -eq 1) { [byte][char]$HoldKey.ToUpper() } else { switch ($HoldKey) { "enter" { 0x0D } "up" { 0x26 } "down" { 0x28 } "left" { 0x25 } "right" { 0x27 } "space" { 0x20 } default { throw "unknown hold key '$HoldKey'" } } }
    if ($HoldAfterPattern -ne "") {
        $deadline = (Get-Date).AddMilliseconds($HoldDelayMs)
        $seen = $false
        while ((Get-Date) -lt $deadline -and -not $proc.HasExited) {
            $tail = Get-Content $LogPath -Tail 40 -ErrorAction SilentlyContinue
            if ($tail -and ($tail | Select-String -Pattern $HoldAfterPattern -Quiet)) { $seen = $true; break }
            Start-Sleep -Milliseconds 500
        }
        if ($seen) { Write-Output ">> log matched '$HoldAfterPattern'" } else { Write-Output "!! log never matched '$HoldAfterPattern' within $HoldDelayMs ms" }
        Start-Sleep -Milliseconds 1500
    } elseif ($HoldDelayMs -gt 0) { Start-Sleep -Milliseconds $HoldDelayMs }
    Write-Output ">> holding $HoldKey for $HoldMs ms"
    if (-not (Focus-Game $hwnd)) { Write-Output "!! the game is not in the foreground for the hold" }
    [DriveGameNative]::keybd_event([byte]$hold, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds $HoldMs
    [DriveGameNative]::keybd_event([byte]$hold, 0, 2, [IntPtr]::Zero)
}
if ($AfterHoldKeys.Count -gt 0 -and -not $proc.HasExited -and $hwnd -ne [IntPtr]::Zero) {
    Start-Sleep -Milliseconds $BetweenKeysMs
    Press-Keys $AfterHoldKeys
}
if (-not $proc.HasExited) {
    if ($StopPattern -ne "") {
        $deadline = (Get-Date).AddMilliseconds($TailWaitMs)
        while ((Get-Date) -lt $deadline -and -not $proc.HasExited) {
            $tail = Get-Content $LogPath -Tail 40 -ErrorAction SilentlyContinue
            if ($tail -and ($tail | Select-String -Pattern $StopPattern -Quiet)) {
                Write-Output ">> log matched '$StopPattern'"
                Start-Sleep -Milliseconds 1000
                break
            }
            Start-Sleep -Milliseconds 500
        }
    } else {
        Start-Sleep -Milliseconds $TailWaitMs
    }
}

if ($proc.HasExited) {
    Write-Output "=== the game exited, code $($proc.ExitCode) ==="
} else {
    Write-Output "=== still running after the last key; stopping it ==="
    $proc.Kill()
    $proc.WaitForExit()
}

$faults = Get-Content $LogPath -ErrorAction SilentlyContinue |
          Select-String -Pattern "exception 0x|unimplemented kernel import|does not implement"
if ($faults) {
    Write-Output "=== faults ==="
    $faults | Select-Object -Last 20
} else {
    Write-Output "=== no faults reported ==="
}

Write-Output "=== full log: $LogPath ==="
