#include "Menu.h"

// Menu (Send, SendEx, SendMessage), Iris (Start, Play), Wheel etc

#define sp_level (*(M_ITEM(*)[12])0x0017c580)

// AUTOINJECT
int Menu_GetLevelIndex(HASHCODE level) {

    for(int i = 0; i < ARRAY_SIZE(sp_level); i++) {
        if(sp_level[i].identifier == level) {
            return i;
        }
    }
    return -1;
}