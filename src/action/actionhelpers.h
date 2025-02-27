#ifndef ACTIONHELPERS_H
#define ACTIONHELPERS_H

#include "assets.h"
#include "../helpers.h"

#include "game/obj/object.h" // for obj_tag needed by some autogen functions
#include "game/obj/player.h" // for BLData

// TODO: Unfinished
typedef struct {
    char pad[0x76];
} M_MANAGER;

// TODO: Unfinished
typedef struct {
    char pad[0x18];
    uint hashcode;
} M_CONTROL;

// Common between PS2 and Xbox
typedef struct {
    HASHCODE iconHashcode;
    Nightfire_TranslatedText title;
    Nightfire_TranslatedText description;
    uint identifier; // Identifier or index
    uint enabled; // 4-byte bool? Upper 3 bits seem unused
    Nightfire_TranslatedText descriptionWhenDisabled;
} M_ITEM;

// TODO: Unfinished
typedef struct {
    char unknown;
} celglist_tag;

// TODO: Unfinished
typedef struct {
    char unknown;
} level_tag;

#endif //ACTIONHELPERS_H
