#pragma once

// Calls into parts of the game our code uses but has not reimplemented, each reaching the original. The classes
// have no fields mapped here; they are only somewhere for the methods to live, under Ghidra's names.

// The HUD (Ghidra: GHud).
class GHud {
public:
    // The one HUD, or null before it exists (0x000d7c10; the global at 0x0023f44c).
    // AUTOGEN
    static GHud *TheApp();

    // Shows or clears the "please reconnect the controller" state; showing it pauses the game (0x000e00f0).
    // AUTOGEN
    void SetControllerUnplugged(bool unplugged);
};
