#pragma once

// The multiplayer match functions of game/mp/multiplayer.cpp, ours against the original on every call
// (MPMatchShadow=on in settings.ini).
void MPMatchShadow_Install(void);
// Once a frame from the devtools tick: MP_Update, original against ours, with the state put back.
void MPMatchShadow_Tick(void);
