#ifndef DRIVING_GAME_HUDMENUS_H_
#define DRIVING_GAME_HUDMENUS_H_

// GHud's menus and boxed messages (HudMenus.cpp): the pause menu's pages and their button hints, the mission
// result's box, the button message and the objective message. Declared in Hud.h.

#include "Hud.h"

// missionResultState (names ours): the box opens to its first key, then its second, the text fades in, holds until
// the speech is done, then fades (a failed mission pauses into its menu as it goes)
enum HudResultState : int32_t {
    kResultNone = 0,
    kResultOpening = 1,
    kResultWidening = 2,
    kResultFadingIn = 3,
    kResultHolding = 4,
    kResultFading = 5,
};

#endif // DRIVING_GAME_HUDMENUS_H_
