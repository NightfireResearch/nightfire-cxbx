#include "GcAnim.h"
#include "../game.h"

// GC 0x80045298
HASHCODE GcFixupSkinHashcode(HASHCODE skin) {
    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_PowerStationA1:
        if (skin == HT_Skin_Elite_B)
            return HT_Skin_Elite;
        break;
    case HT_Level_EvilBase:
        if (skin == HT_Skin_Elite_B)
            return HT_Skin_Elite_Beret;
        break;
    }
    return skin;
}
