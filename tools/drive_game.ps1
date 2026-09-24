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

$proc = Start-Process -FilePath $exePath -WorkingDirectory $workDir -PassThru `
    -RedirectStandardOutput $LogPath -RedirectStandardError "$LogPath.err"

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

if ($hwnd -eq [IntPtr]::Zero) {
    Write-Output "!! no game window found - keys would go to whatever is in front, so not sending any"
} else {
    [void][DriveGameNative]::ShowWindow($hwnd, 5)
    [void][DriveGameNative]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 700

    if ([DriveGameNative]::GetForegroundWindow() -ne $hwnd) {
        Write-Output "!! could not bring the game to the foreground; its focus check will ignore the keys"
    }

    foreach ($key in $Keys) {
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
            default  {
                if ($key.Length -ne 1) { throw "unknown key '$key' - see the key names in this script's header" }
                [byte][char]$key.ToUpper()
            }
        }

        Write-Output ">> $key"
        [DriveGameNative]::keybd_event([byte]$vk, 0, 0, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 120
        [DriveGameNative]::keybd_event([byte]$vk, 0, 2, [IntPtr]::Zero)   # 2 = KEYEVENTF_KEYUP
        Start-Sleep -Milliseconds $BetweenKeysMs
    }
}

if (-not $proc.HasExited) { Start-Sleep -Milliseconds $TailWaitMs }

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
