// What drones say: the death cry, the lines said to the player when he looks at a drone, the patrol chatter, the
// Powerstation loudspeakers and the conversation in CastleIndoors2. The speech itself is NDrone2_SpeechSFX (still
// the game's), whose numbers index Drone_SfxInfo (docs/drone/behaviours/drone_sfx_info.csv).

#include "NDrone2.h"
#include "NDrone2_Combat.h"
#include "../../engine/Anim.h"
#include "DroneTables.h"
#include "Behaviour.h"
#include "../../game.h"                 // GameState
#include "../../math/math.h"            // Vec_* (ours)
#include "../../util/Random.h"          // Rand_Rand
#include "../sp/SwitchChannels.h"       // switch_channels
#include "../mp/multiplayer.h"          // MPSettings
#include "../../sound/Sound.h"          // Sound_Play3D
#include "../../util/hashtable.h"       // hashtable_getitem
#include "DroneVision.h"
#include "NDrone2_Opponent.h"

#include <math.h>
#include <stdint.h>
#include "BOT.h"
#include "../../math/X87Ops.h"

// One entry of Drone_SfxInfo, per speech or sound number (docs/drone/behaviours/drone_sfx_info.csv)
typedef struct DroneSfxInfo {
    uint sfx;               // 0x00 - the sound (Action_SFX); 0 for none
    HASHCODE facialAnim;    // 0x04 - the facial script said instead, when a face is asked for
    uint lastFrame;         // 0x08 - when it was last played
    uint interval;          // 0x0c - frames (at 60 a second) before it plays again
    float volume;           // 0x10
    float radiusOuter;      // 0x14
    float radiusInner;      // 0x18
    short pitchMin;         // 0x1c - the pitch is drawn between these
    short pitchMax;         // 0x1e
} DroneSfxInfo;
static_assert(sizeof(DroneSfxInfo) == 0x20, "DroneSfxInfo is 0x20 bytes");

// XBE_GLOBAL(0x00174b28, 0x2b40)
DroneSfxInfo Drone_SfxInfo[DRONE_SFX_COUNT] = {
    {.interval = 30, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GENERIC_GUARD_DIE, .interval = 30, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_DRAKE_DEATH, .interval = 30, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GENERIC_GUARD_DIE, .interval = 30, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GENERIC_GUARD_DIE, .interval = 30, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GENERIC_GUARD_DIE, .interval = 30, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GENERIC_GUARD_DIE, .interval = 30, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_MAYHEW_PAIN, .interval = 30, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_ROOK_DEATH, .interval = 30, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_HOSTAGE_GIRL_DEATH, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_HOSTAGE_GIRL_DEATH, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_ALURA_DEATH, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_ALURA_DEATH, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_ZOE_DEATH, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_KIKO_DEATH, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GENERIC_SOLDIER_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_DRAKE_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GENERIC_SOLDIER_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GENERIC_SOLDIER_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GENERIC_GUARD_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GENERIC_SOLDIER_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_MAYHEW_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_ROOK_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GEN_HOSTAGE_PAIN_SERV, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GEN_HOSTAGE_PAIN_SERV, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GEN_HOSTAGE_PAIN_SERV, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GEN_HOSTAGE_PAIN_SERV, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_ZOE_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GEN_HOSTAGE_PAIN_SERV, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GENERIC_SOLDIER_PAIN, .interval = 30, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GENERIC_GUARD_PAIN, .interval = 30, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GENERIC_GUARD_PAIN, .interval = 30, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {
        .sfx = SFX_CHR_SPEECH_GEN_YAK_COMBAT_YAK19,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {.sfx = SFX_ENV_BOOT_DOOR, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_ENV_TABLE_TOPPLE, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M2_INTER_YAK1, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M2_INTER_YAK2, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M2_INTER_YAK3, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M2_INTER_YAK6, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M2_INTER_YAK9, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M2_INTER_SERV10, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M2_INTER_SERV6, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M2_INTER_SERV7, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M2_INTER_SERV8, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {
        .sfx = SFX_CHR_SPEECH_GEN_YAK_COMBAT_YAK11,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_GEN_YAK_COMBAT_YAK16,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_GEN_YAK_COMBAT_YAK20,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_GEN_YAK_COMBAT_YAK18,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA01,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA03,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA05,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA11,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA18,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA19,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA20,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA21,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA24,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M2_MAY_SCOLDING_MAYHEW12,
        .facialAnim = HT_Script_Speech_M2_MAY_SCOLDING_MAYHEW12,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M2_MAY_SCOLDING_MAYHEW3,
        .facialAnim = HT_Script_Speech_M2_MAY_SCOLDING_MAYHEW3,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M2_MAY_SCOLDING_MAYHEW6,
        .facialAnim = HT_Script_Speech_M2_MAY_SCOLDING_MAYHEW6,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M2_MAY_SCOLDING_MAYHEW8,
        .facialAnim = HT_Script_Speech_M2_MAY_SCOLDING_MAYHEW8,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {.sfx = SFX_CHR_SPEECH_M2_BUNKER_FAIL_MAYHEW13, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M2_MAY_DIR_MAYHEW14, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M2_MAY_DIR_MAYHEW15, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M2_MAY_DIR_MAYHEW19, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_HOSTAGE_GIRL_SCREAM, .interval = 300, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA17, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA18, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA20, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA21, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA12, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA04, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_INT_GUARD6,
        .facialAnim = HT_Script_Speech_CAS_IG_INT_GUARD6,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_INT_GUARD9,
        .facialAnim = HT_Script_Speech_CAS_IG_INT_GUARD9,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_INT_GUARD8_B,
        .facialAnim = HT_Script_Speech_CAS_IG_INT_GUARD8_B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_KIKO02,
        .facialAnim = HT_Script_Speech_CAS_IG_KIKO02,
        .interval = 960,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_KIKOBOND1A,
        .facialAnim = HT_Script_Speech_CAS_IG_KIKOBOND1A,
        .interval = 240,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_KIKOBOND1B,
        .interval = 960,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_KIKOBOND2A,
        .facialAnim = HT_Script_Speech_CAS_IG_KIKOBOND2A,
        .interval = 280,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_KIKOBOND2B,
        .interval = 240,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_KIKO03,
        .facialAnim = HT_Script_Speech_CAS_IG_KIKO03,
        .interval = 300,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F1_AUS,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F1_AUS,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F1_BRIT,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F1_BRIT,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F1_RUS,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F1_RUS,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F1_USA,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F1_USA,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F2_AUS,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F2_AUS,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F2_BRIT,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F2_BRIT,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F2_RUS,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F2_RUS,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F2_USA,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F2_USA,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F2B_AUS,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F2B_AUS,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F2B_BRIT,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F2B_BRIT,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F2B_RUS,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F2B_RUS,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F2B_USA,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F2B_USA,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F3_AUS,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F3_AUS,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F3_BRIT,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F3_BRIT,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F3_RUS,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F3_RUS,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F3_USA,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F3_USA,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F4_AUS,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F4_AUS,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F4_BRIT,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F4_BRIT,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F4_RUS,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F4_RUS,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F4_USA,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F4_USA,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F5_AUS,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F5_AUS,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F5_BRIT,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F5_BRIT,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F5_RUS,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F5_RUS,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F5_USA,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F5_USA,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F6_AUS,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F6_AUS,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F6_BRIT,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F6_BRIT,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F6_RUS,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F6_RUS,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F6_USA,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_F6_USA,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F7_AUS, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F7_BRIT, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F7_RUS, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F7_USA, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F8_AUS, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F8_BRIT, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F8_RUS, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F8_USA, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F9_AUS, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F9_BRIT, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F9_RUS, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F9_USA, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F10_AUS, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F10_BRIT, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F10_RUS, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_F10_USA, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_M2_E,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_M2_E,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_M3_E,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_M3_E,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_M4_E,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_M4_E,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_M5_E,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_M5_E,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_M6_E,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_M6_E,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_M7_E,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_M7_E,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_M8_E,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_M8_E,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_M9_E,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_M9_E,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_GUEST_M10_E,
        .facialAnim = HT_Script_Speech_CAS_IG_GUEST_M10_E,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {.interval = 900, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_INT_GUARD5_B,
        .facialAnim = HT_Script_Speech_CAS_IG_INT_GUARD5_B,
        .interval = 900,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_INT_GUARD10_B,
        .facialAnim = HT_Script_Speech_CAS_IG_INT_GUARD10_B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_INT_GUARD11,
        .facialAnim = HT_Script_Speech_CAS_IG_INT_GUARD11,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_INT_GUARD12,
        .facialAnim = HT_Script_Speech_CAS_IG_INT_GUARD12_B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_INT_GUARD13,
        .facialAnim = HT_Script_Speech_CAS_IG_INT_GUARD13,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_INT_GUARD18_B,
        .facialAnim = HT_Script_Speech_CAS_IG_INT_GUARD18_B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_INT_GUARD19_B,
        .facialAnim = HT_Script_Speech_CAS_IG_INT_GUARD19_B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_MAYHEW1,
        .facialAnim = HT_Script_Speech_CAS_IG_MAYHEW1,
        .interval = 900,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_MAYHEW4,
        .facialAnim = HT_Script_Speech_CAS_IG_MAYHEW4,
        .interval = 900,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_ROOK1,
        .facialAnim = HT_Script_Speech_CAS_IG_ROOK1,
        .interval = 300,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_ROOK2,
        .facialAnim = HT_Script_Speech_CAS_IG_ROOK2,
        .interval = 300,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_ROOK3,
        .facialAnim = HT_Script_Speech_CAS_IG_ROOK3,
        .interval = 300,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_CAS_IG_ROOK4_ALT,
        .facialAnim = HT_Script_Speech_CAS_IG_ROOK4_ALT,
        .interval = 300,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M1_RESCUE_ZOE7,
        .interval = 60,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {.sfx = SFX_CHR_SPEECH_CAS_IG_ZOE6B2, .interval = 60, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_ZOE06B, .interval = 60, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_CAS_IG_ZOE08, .interval = 60, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {
        .sfx = SFX_CHR_SPEECH_M2_INTER_SERV3,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M2_INTER_SERV4,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M2_INTER_SERV5,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_M2_BATH_SERV1,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_GEN_YAK_COMBAT_YAK11,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_GEN_YAK_COMBAT_YAK16,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_GEN_YAK_COMBAT_YAK20,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {.sfx = SFX_CHR_SPEECH_M3_YAK_001, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M3_YAK_002, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M3_YAK_003, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M3_YAK_004, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M3_YAK_005, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_001, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_006, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_007, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_008, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_013, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_015, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_016, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_009, .interval = 600, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_010, .interval = 600, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_011, .interval = 600, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_012, .interval = 600, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_014, .interval = 600, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_019, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_020, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_021, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_022, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_023, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_017, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_018, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_002, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_003, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_004, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M5_BO1_005, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M3_GUA_002, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M3_GUA_011, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M3_GUA_012, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M3_GUA_017, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M3_GUA_019, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M3_GUA_020, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M3_GUA_021, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M3_GUA_022, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_M3_GUA_023, .interval = 180, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_40B,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_41B,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_43B,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_44B,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_45B,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_46B,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_42B,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_2B,
        .interval = 600,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_3B,
        .interval = 600,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_4B,
        .interval = 600,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_10B,
        .interval = 600,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_1B,
        .interval = 600,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_11B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_14B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_16B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_17B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_19B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_20B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_21B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_27B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_29B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_30B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_32B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_35B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_36B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_37B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_38B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_39B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_NUK_IG_SNIP2_9B,
        .interval = 180,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_AST_ROOK_TAUNT13,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_AST_ROOK_TAUNT14,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_AST_ROOK_TAUNT4,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_AST_ROOK_TAUNT7,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_AST_ROOK_TAUNT10,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_AST_ROOK_TAUNT8,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_AST_ROOK_TAUNT9,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_AST_ROOK_TAUNT6,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_AST_ROOK_TAUNT12,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {
        .sfx = SFX_CHR_SPEECH_AST_ROOK_TAUNT5,
        .interval = 120,
        .volume = 100.0f,
        .radiusOuter = 15.0f,
        .radiusInner = 10.0f,
    },
    {.sfx = SFX_CHR_SPEECH_USB_PHX_20, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_25, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_20, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_25, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_21, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_26, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_21, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_26, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_24, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_29, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_24, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_29, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_31, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_35, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_31, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_35, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_34, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_36, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_34, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_36, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_40, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_40, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_41, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_41, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_44, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_44, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_1, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_8, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_1, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_8, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_10, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_11, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_12, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_3, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_4, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_10, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_11, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_12, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_3, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_4, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_13, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_14, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_15, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_6, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_16, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_19, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_5, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_13, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_14, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_15, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_6, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_16, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_19, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_5, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_17, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_18, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_2, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_7, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_9, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_17, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_18, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_2, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_7, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_9, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_45, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_46, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_48, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_49, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_50, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_45, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_46, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_48, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_49, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_50, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_47, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_47, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_51, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_51, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_52, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_53, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_52, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_53, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_54, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_55, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_56, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX_57, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_54, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_55, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_56, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_SPEECH_USB_PHX3_57, .interval = 120, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_DRAKE_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_BLACK_OPS_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GEN_YAK_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_MAYHEW_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GENERIC_GUARD_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_ALURA_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_ZOE_PAIN, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_DRAKE_DEATH, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_BLACK_OPS_DEATH, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_GENERIC_GUARD_DIE, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_ALURA_DEATH, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_HOSTAGE_GIRL_DEATH, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
    {.sfx = SFX_CHR_ZOE_DEATH, .volume = 100.0f, .radiusOuter = 15.0f, .radiusInner = 10.0f},
};

// The entry of a number, or NULL past the table (which the callers then read)
static DroneSfxInfo *SfxInfo(uint sfx) {
    return sfx < DRONE_SFX_COUNT ? &Drone_SfxInfo[sfx] : NULL;
}

// No drone sound for the first 30 frames (at 60 a second) after the level is loaded; worked out on the x87
static bool TooSoonAfterLoad(uint now) {
    return (double)now < (double)FRAME_RATE_DIV * 30.0f + (double)NPCGlobals.postLoadFrame;
}

// Whether the entry played less than its interval ago (__ftol2 of the frame it may play again)
static bool PlayedTooRecently(const DroneSfxInfo *info, uint now) {
    return now < (uint)Ftol((double)info->interval * FRAME_RATE_DIV + (double)info->lastFrame);
}

// One Powerstation loudspeaker line: said once its channel comes on (the channel differs between the two levels)
typedef struct PowerstationLine {
    ushort channelA1;       // in PowerStationA1
    ushort channelA2;       // in PowerStationA2
    uint sfx;               // 0 ends the table
} PowerstationLine;
static_assert(sizeof(PowerstationLine) == 8, "PowerstationLine is 8 bytes");

// XBE_GLOBAL(0x00177668, 0x98)
const PowerstationLine PowerstationLines[19] = {
    {.channelA2 = 70, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_11B},
    {.channelA1 = 52, .channelA2 = 71, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_14B},
    {.channelA2 = 72, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_16B},
    {.channelA1 = 64, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_17B},
    {.channelA1 = 53, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_19B},
    {.channelA1 = 54, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_20B},
    {.channelA1 = 55, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_21B},
    {.channelA1 = 56, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_27B},
    {.channelA1 = 57, .channelA2 = 74, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_29B},
    {.channelA1 = 58, .channelA2 = 75, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_30B},
    {.channelA1 = 59, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_32B},
    {.channelA1 = 60, .channelA2 = 77, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_35B},
    {.channelA1 = 79, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_36B},
    {.channelA1 = 80, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_37B},
    {.channelA2 = 81, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_38B},
    {.channelA1 = 61, .channelA2 = 83, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_39B},
    {.channelA1 = 62, .sfx = DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_9B},
    {},
    {.channelA1 = 256},
};

// HandleTalking's countdown between tries (only it uses it)
// XBE_GLOBAL(0x001ee41c, 4)
// XBE_GLOBAL(0x001ee41c, 0x4)
int32_t HandleTalkingDelay;

// The frame `ticks` (at 60 a second) from now, worked out on the x87 and truncated (__ftol2)
static uint TalkFrameAfter(double ticks) {
    return (uint)(long long)((double)FRAME_RATE_DIV * ticks + (double)(uint)GameState.NumFramesUnpaused);
}

// Whether the line is the one the drone is saying; with nothing said, whether its sound is 0xffffffff
// FUNC_AT(0x00036230)
bool Drone_IsSayingSfx(Drone_tag *drone, uint sfx) {
    if (drone->speechChannel == NULL)
        return Drone_SfxInfo[sfx].sfx == 0xffffffff;
    return Drone_SfxInfo[sfx].sfx == (drone->speechChannel->sfxId & 0xfffff);
}

// Plays a facial script on the drone, which tells facialAnimDone when it stops and clears facialAnim when deleted
// AUTOINJECT
void NDrone2_PlayFacialAnim(DCVars_tag *dcv, HASHCODE script) {
    Drone_tag *drone = dcv->drone;
    sAnimScript_tag *anim = AnimFacialAppend(dcv->gameObj, script);
    drone->facialAnim = anim;
    if (anim != NULL) {
        drone->facialAnimDone = 0;
        drone->facialAnimPlaying = 1;
        anim->doneFlag = &drone->facialAnimDone;
        anim->ownerSlot = (sAnimScript_tag **)&drone->facialAnim;
        anim->flags &= ~ANIMSCRIPT_LOOP;
        anim->playMode = ANIMPLAY_DELETE;
    }
    drone->unknown37 = 0;
}

// Says the line when Rand_Rand(0) gives 0 (it always does, but draws), and keeps the frame 5 s on. FUN_0003b830 in
// Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
bool DroneFunc_InjuredSFX(DCVars_tag *dcv, uint sfx) {
    if (Rand_Rand(0) != 0)
        return false;
    NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    NPCGlobals.injuredTalkFrame = GameState.NumFramesUnpaused + FRAME_RATE_INT * 5;
    return true;
}

// Whether the drone is still saying something; a line that has finished is let go.
// AUTOINJECT
bool Drone_IsTalking(Drone_tag *drone) {
    if (drone->speechChannel == NULL)
        return false;
    if (!Sound_IsFinnished(drone->speechChannel))
        return true;
    drone->speechChannel = NULL;
    return false;
}

// The death cry, by skin class; never in multiplayer, and not when it is the line being said already. Skin classes
// 5 and 6 in the Powerstation levels pick one of two, and neither if either is being said.
// AUTOINJECT
bool NDrone2_DeathTalk(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (MPSettings.dronesAreBots != 0)
        return false;

    uint sfx = DRONE_SFX_CHR_GENERIC_GUARD_DIE;
    switch ((ushort)drone->skinClass) {
    case SKINCLASS_YAKUZA:
        sfx = DRONE_SFX_CHR_GENERIC_GUARD_DIE_3;
        break;
    case SKINCLASS_SNOW_GUARD:
        sfx = DRONE_SFX_CHR_GENERIC_GUARD_DIE_4;
        break;
    case SKINCLASS_BLACK_OPS:
        sfx = DRONE_SFX_CHR_GENERIC_GUARD_DIE_2;
        break;
    case SKINCLASS_SNIPER:
    case SKINCLASS_ELITE:
        if (GameState.CurrentLevelHashcode == HT_Level_PowerStationA1 ||
            GameState.CurrentLevelHashcode == HT_Level_PowerStationA2) {
            if (Drone_IsSayingSfx(drone, DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_41B) ||
                Drone_IsSayingSfx(drone, DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_42B))
                return false;
            sfx = Rand_Rand(2) != 0 ? DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_41B : DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_42B;
        }
        break;
    case SKINCLASS_SERVANT:
    case 10:
        sfx = DRONE_SFX_CHR_HOSTAGE_GIRL_DEATH_2;
        break;
    case SKINCLASS_ZOE:
        sfx = DRONE_SFX_CHR_ZOE_DEATH;
        break;
    case SKINCLASS_ROOK:
        sfx = DRONE_SFX_CHR_ROOK_DEATH;
        break;
    case SKINCLASS_KIKO:
    case SKINCLASS_KIKO_COMBAT:
        sfx = DRONE_SFX_CHR_KIKO_DEATH;
        break;
    case SKINCLASS_DRAKE:
        sfx = DRONE_SFX_CHR_DRAKE_DEATH;
        break;
    default:
        sfx = drone->female ? DRONE_SFX_CHR_HOSTAGE_GIRL_DEATH : DRONE_SFX_CHR_GENERIC_GUARD_DIE;
        break;
    }

    if (Drone_IsSayingSfx(drone, sfx))
        return false;
    NDrone2_SpeechSFX(dcv, sfx, 1, 0, 0);
    return true;
}

// A line on being hit: skin classes 5 to 7 in EvilBase, EvilSilo and EvilBaseC, and 16. Never in multiplayer, while
// it is still talking or once dead; for 3 s after sfxFrame a hit says nothing one time in five.
// AUTOINJECT
bool NDrone2_HitTalkExtra(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (MPSettings.dronesAreBots != 0)
        return false;
    if (Drone_IsTalking(drone))
        return false;
    if (drone->health <= 0.0f)
        return false;
    if (drone->sfxFrame + FRAME_RATE_INT * 3 > GameState.NumFramesUnpaused && Rand_Rand(5) == 0)
        return false;

    uint sfx = 0;
    ushort skinClass = drone->skinClass;
    if (skinClass >= SKINCLASS_SNIPER && skinClass <= SKINCLASS_ELITE_COMBAT) {
        if (GameState.CurrentLevelHashcode == HT_Level_EvilBase ||
            GameState.CurrentLevelHashcode == HT_Level_EvilBaseC || GameState.CurrentLevelHashcode == HT_Level_EvilSilo)
            sfx = DroneFunc_ChooseSFXByID(drone, DRONE_SFX_CHR_SPEECH_USB_PHX_45, DRONE_SFX_CHR_SPEECH_USB_PHX_50);
    } else if (skinClass == SKINCLASS_ROOK) {
        sfx = DroneFunc_ChooseSFX(DRONE_SFX_CHR_SPEECH_AST_ROOK_TAUNT7, DRONE_SFX_CHR_SPEECH_AST_ROOK_TAUNT5);
    }
    if (sfx == 0)
        return false;
    NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    return true;
}

// The cry of pain, by skin class (16 says its hit line instead when it can); never in multiplayer, and not when it is
// the line being said already.
// AUTOINJECT
bool NDrone2_PainTalk(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (MPSettings.dronesAreBots != 0)
        return false;

    uint sfx;
    switch ((ushort)drone->skinClass) {
    case SKINCLASS_YAKUZA:
        sfx = DRONE_SFX_CHR_GENERIC_SOLDIER_PAIN_3;
        break;
    case SKINCLASS_SNOW_GUARD:
        sfx = DRONE_SFX_CHR_GENERIC_GUARD_PAIN;
        break;
    case SKINCLASS_BLACK_OPS:
        sfx = DRONE_SFX_CHR_GENERIC_SOLDIER_PAIN_2;
        break;
    case SKINCLASS_ELITE_COMBAT:
        sfx = DRONE_SFX_CHR_GENERIC_SOLDIER_PAIN_4;
        break;
    case SKINCLASS_SERVANT:
    case 10:
        sfx = DRONE_SFX_CHR_GEN_HOSTAGE_PAIN_SERV_2;
        break;
    case SKINCLASS_MAYHEW:
        sfx = DRONE_SFX_CHR_MAYHEW_PAIN_2;
        break;
    case SKINCLASS_ZOE:
        sfx = DRONE_SFX_CHR_ZOE_PAIN;
        break;
    case SKINCLASS_ROOK:
        if (NDrone2_HitTalkExtra(dcv))
            return false;
        sfx = DRONE_SFX_CHR_ROOK_PAIN;
        break;
    case SKINCLASS_KIKO:
    case SKINCLASS_KIKO_COMBAT:
        sfx = DRONE_SFX_CHR_GEN_HOSTAGE_PAIN_SERV_5;
        break;
    case SKINCLASS_DRAKE:
        sfx = DRONE_SFX_CHR_DRAKE_PAIN;
        break;
    default:
        sfx = drone->female ? DRONE_SFX_CHR_GEN_HOSTAGE_PAIN_SERV : DRONE_SFX_CHR_GENERIC_SOLDIER_PAIN;
        break;
    }

    if (Drone_IsSayingSfx(drone, sfx))
        return false;
    NDrone2_SpeechSFX(dcv, sfx, 1, 0, 0);
    return true;
}

// A line on being hurt, by level: only in multiplayer (as the original has it), and not while it is still talking or
// once dead. Skin class 16 says nothing in EvilBase, EvilSilo and EvilBaseC.
// AUTOINJECT
void DroneFunc_InjuredTalk(DCVars_tag *dcv) {
    if (MPSettings.dronesAreBots == 0)
        return;
    Drone_tag *drone = dcv->drone;
    if (Drone_IsTalking(drone))
        return;
    if (drone->health <= 0.0f)
        return;

    uint sfx;
    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_PowerStationA1:
    case HT_Level_PowerStationA2:
        sfx = DroneFunc_ChooseSFX(DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_40B, DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_46B);
        break;
    case HT_Level_Tower2A:
    case HT_Level_Tower2B:
    case HT_Level_Tower2C:
    case HT_Level_Tower2Elevator:
        sfx = DRONE_SFX_CHR_SPEECH_M5_BO1_020;
        break;
    case HT_Level_EvilBase:
    case HT_Level_EvilSilo:
    case HT_Level_EvilBaseC:
        if (drone->skinClass == SKINCLASS_ROOK)
            return;
        sfx = DroneFunc_ChooseSFXByID(drone, DRONE_SFX_CHR_SPEECH_USB_PHX_45, DRONE_SFX_CHR_SPEECH_USB_PHX_50);
        break;
    default:
        return;
    }
    if (sfx != 0)
        DroneFunc_InjuredSFX(dcv, sfx);
}

// A line said to the player while he looks at the drone (inPlayersSights), turning its head to him for trackTicks:
// then nothing more for waitTicks (at 60 a second), and only once while he keeps looking unless `repeat` is set.
// stateScratch counts the lines, stateScratch2 is the next frame it may speak, stateScratch3 the lines since he
// started looking; 0xff in stateScratch (a state just entered) clears all three once he looks. True when it spoke,
// or tried: a line NDrone2_SpeechSFX turned down with no facial anim playing counts too, without the wait.
// AUTOINJECT
bool DroneFunc_DoBondTalk(DCVars_tag *dcv, uint sfx, int trackTicks, uint waitTicks, char repeat) {
    Drone_tag *drone = dcv->drone;
    obj_tag *player = NDrone2_Player();

    if (drone->stateScratch == 0xff && drone->inPlayersSights) {
        drone->stateScratch = 0;
        drone->stateScratch2 = 0;
        drone->stateScratch3 = 0;
    }
    if (!drone->inPlayersSights) {
        if (!repeat)
            drone->stateScratch = 0;
        drone->stateScratch3 = 0;
    }

    if (Drone_IsTalking(drone))
        return false;
    if (!drone->inPlayersSights || drone->stateScratch2 > GameState.NumFramesUnpaused)
        return false;
    if (drone->stateScratch3 != 0 && !repeat)
        return false;

    NDrone2_HeadTrackObj(player, trackTicks, 0.0f, dcv);
    if (NDrone2_SpeechSFX(dcv, sfx, 0, 1, 1) != 0 || drone->facialAnim != NULL) {
        drone->stateScratch++;
        drone->stateScratch2 = TalkFrameAfter(waitTicks);
        drone->stateScratch3++;
    }
    return true;
}

// What the guests in CastleIndoors1 say to the player, from the civilian, party girl and guard states: only while
// the drone is drawn, unarmed, within 3 m of the player and facing him. By skin class:
//   9:  0x95 or 0x96, once while he looks at it; again only after the state is entered anew
//   13: one of eleven lines in the skin's voice every 10 s while he looks (the first within a second)
//   14: one of 0x84-0x8c through DroneFunc_DoBondTalk
//   16: one of 0x97-0x9a every 8 s while he looks
//   17: 0x8e every 8 s while he looks (the first within a second)
//   18: a six-line conversation, whether he looks or not, 2 s after the state is entered
// False when it is not the time or place (or the conversation is over), else true.
// AUTOINJECT
bool NDrone2_BondTalk(DCVars_tag *dcv) {
    obj_tag *gameObj = dcv->gameObj;
    Drone_tag *drone = dcv->drone;
    if (!gameObj->drawnCountdown)
        return false;
    if (drone->distanceToTarget > 3.0f)
        return false;
    if (fabsf(drone->opponentBearing) > (float)M_PI_2)
        return false;
    if (NDrone2_ObjectIsArmed(gameObj))
        return false;
    if (GameState.CurrentLevelHashcode != HT_Level_CastleIndoors1)
        return false;
    obj_tag *player = NDrone2_Player();
    if (player == NULL)
        return false;

    uint now = GameState.NumFramesUnpaused;
    switch ((ushort)drone->skinClass) {
    case SKINCLASS_MAYHEW:
        if (Drone_IsTalking(drone))
            return true;
        if (drone->stateScratch == 0xff) {
            if (drone->inPlayersSights) {
                drone->stateScratch = 0;
                drone->stateScratch2 = 0;
            }
            return true;
        }
        if (!drone->inPlayersSights || drone->stateScratch >= 1 || drone->stateScratch2 >= now)
            return true;
        NDrone2_HeadTrackObj(player, 200, 0.0f, dcv);
        if (NDrone2_SpeechSFX(dcv, DRONE_SFX_CHR_SPEECH_CAS_IG_MAYHEW1 + Rand_Rand(2), 0, 1, 1) == 0 &&
            drone->facialAnim == NULL)
            return true;
        drone->stateScratch2 = TalkFrameAfter(480.0f);
        drone->stateScratch = 1;
        return true;

    case SKINCLASS_PARTY_GIRL: {
        if (Drone_IsTalking(drone))
            return true;
        if (drone->stateScratch == 0xff) {
            if (drone->inPlayersSights) {
                drone->stateScratch = Rand_Rand(7);
                drone->stateScratch2 = Rand_Rand(FRAME_RATE_INT) + GameState.NumFramesUnpaused;
            }
            return true;
        }
        if (!drone->inPlayersSights || drone->stateScratch >= 7 || drone->stateScratch2 >= now)
            return true;
        ushort voice;
        switch ((uint)drone->skinHashcode) {
        case HT_Skin_PartyGirl01:
            voice = 0;
            break;
        case HT_Skin_PartyGirl02:
            voice = 1;
            break;
        case HT_Skin_PartyGirl03:
            voice = 2;
            break;
        default:
            voice = 3;
            break;
        }
        NDrone2_HeadTrackObj(player, 200, 0.0f, dcv);
        if (NDrone2_SpeechSFX(dcv, Rand_Rand(11) * 4 + voice + DRONE_SFX_CHR_SPEECH_CAS_IG_GUEST_F1_AUS,
                              0, 1, 1) == 0 && drone->facialAnim == NULL)
            return true;
        drone->stateScratch2 = TalkFrameAfter(600.0f);
        drone->stateScratch = Rand_Rand(7);
        return true;
    }

    case SKINCLASS_PARTY_GUEST:
        DroneFunc_DoBondTalk(dcv, DroneFunc_ChooseSFX(DRONE_SFX_CHR_SPEECH_CAS_IG_GUEST_M2_E,
                                                      DRONE_SFX_CHR_SPEECH_CAS_IG_GUEST_M10_E),
                             200, 480, 0);
        return true;

    case SKINCLASS_ROOK:
        if (Drone_IsTalking(drone))
            return true;
        if (drone->stateScratch == 0xff) {
            if (drone->inPlayersSights) {
                drone->stateScratch = 0;
                drone->stateScratch2 = 0;
            }
            return true;
        }
        if (!drone->inPlayersSights || drone->stateScratch >= 1 || drone->stateScratch2 >= now)
            return true;
        NDrone2_HeadTrackObj(player, 200, 0.0f, dcv);
        if (NDrone2_SpeechSFX(dcv, DRONE_SFX_CHR_SPEECH_CAS_IG_ROOK1 + Rand_Rand(4), 0, 1, 1) == 0 &&
            drone->facialAnim == NULL)
            return true;
        drone->stateScratch2 = TalkFrameAfter(480.0f);
        drone->stateScratch = 0;
        return true;

    case SKINCLASS_CASTLE_GUARD:
        if (Drone_IsTalking(drone))
            return true;
        if (drone->stateScratch == 0xff) {
            if (drone->inPlayersSights) {
                drone->stateScratch = 0;
                drone->stateScratch2 = Rand_Rand(FRAME_RATE_INT) + GameState.NumFramesUnpaused;
            }
            return true;
        }
        if (!drone->inPlayersSights || drone->stateScratch >= 1 || drone->stateScratch2 >= now)
            return true;
        NDrone2_HeadTrackObj(player, 200, 0.0f, dcv);
        if (NDrone2_SpeechSFX(dcv, DRONE_SFX_CHR_SPEECH_CAS_IG_INT_GUARD5_B, 0, 1, 1) == 0 && drone->facialAnim == NULL)
            return true;
        drone->stateScratch2 = TalkFrameAfter(480.0f);
        return true;

    case SKINCLASS_KIKO: {
        if (Drone_IsTalking(drone))
            return true;
        uint line = drone->stateScratch;
        if (line == 0xff) {
            drone->stateScratch = 0;
            drone->stateScratch2 = GameState.NumFramesUnpaused + FRAME_RATE_INT * 2;
            return true;
        }
        if (line >= 7)
            return false;
        if (drone->stateScratch2 >= now)
            return true;

        // Each line: the head turned to the player for so long (or not), the line, and the wait before the next
        double wait;
        switch (line) {
        case SKINCLASS_NONE:
            NDrone2_HeadTrackObj(player, (int)(long long)((double)FRAME_RATE_DIV * 180.0f), 0.0f, dcv);
            if (NDrone2_SpeechSFX(dcv, DRONE_SFX_CHR_SPEECH_CAS_IG_KIKO02, 0, 1, 1) == 0 && drone->facialAnim == NULL)
                return true;
            wait = 960.0f;
            break;
        case SKINCLASS_YAKUZA:
            NDrone2_HeadTrackObj(player, 200, 0.0f, dcv);
            if (NDrone2_SpeechSFX(dcv, DRONE_SFX_CHR_SPEECH_CAS_IG_KIKOBOND1A, 0, 1, 1) == 0 &&
                drone->facialAnim == NULL)
                return true;
            wait = 150.0f;
            break;
        case SKINCLASS_SNOW_GUARD:
            if (NDrone2_SpeechSFX(dcv, DRONE_SFX_CHR_SPEECH_CAS_IG_KIKOBOND1B, 0, 0, 0) == 0 &&
                drone->facialAnim == NULL)
                return true;
            wait = 480.0f;
            break;
        case SKINCLASS_BLACK_OPS:
            NDrone2_HeadTrackObj(player, 240, 0.0f, dcv);
            if (NDrone2_SpeechSFX(dcv, DRONE_SFX_CHR_SPEECH_CAS_IG_KIKOBOND2A, 0, 1, 1) == 0 &&
                drone->facialAnim == NULL)
                return true;
            wait = 240.0f;
            break;
        case SKINCLASS_HAZMAT:
            if (NDrone2_SpeechSFX(dcv, DRONE_SFX_CHR_SPEECH_CAS_IG_KIKOBOND2B, 0, 0, 0) == 0 &&
                drone->facialAnim == NULL)
                return true;
            wait = 240.0f;
            break;
        case SKINCLASS_SNIPER:
            NDrone2_HeadTrackObj(player, 180, 0.0f, dcv);
            if (NDrone2_SpeechSFX(dcv, DRONE_SFX_CHR_SPEECH_CAS_IG_KIKO03, 0, 1, 1) == 0 && drone->facialAnim == NULL)
                return true;
            wait = 300.0f;
            break;
        default:
            // The end: the head let go
            NDrone2_HeadTrackObj(NULL, 600, 0.0f, dcv);
            drone->stateScratch++;
            return true;
        }
        drone->stateScratch2 = TalkFrameAfter(wait);
        drone->stateScratch++;
        return true;
    }

    default:
        return true;
    }
}

// Every live, active drone of type Mayhew says the line. The first such drone that is not active ends the search,
// so those after it say nothing.
// AUTOINJECT
void Drone_MayhewTalk(uint sfx) {
    if (Drone_bDisableSystem)
        return;
    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        if (drone->dtype != DTYPE_Mayhew)
            continue;
        if (MPDrone_IsDyingOrDead(drone->gameObj))
            return;
        DCVars_tag dcv;
        if (Drone_DCVfromOBJ(drone->gameObj, &dcv))
            NDrone2_SpeechSFX(&dcv, sfx, 0, 0, 0);
    }
}

// Now and then a patrolling guard (skin class 3) mutters one of five lines: a chance in two at most every 5 s, then
// none for 10 s, shared by every drone. Never in multiplayer or once dead.
// AUTOINJECT
bool NDrone2_PatrolTalk(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (MPSettings.dronesAreBots != 0)
        return false;
    if (drone->health <= 0.0f)
        return false;
    if (Drone_IsTalking(drone))
        return false;
    if (NPCGlobals.nextPatrolTalkFrame > GameState.NumFramesUnpaused)
        return false;
    if (Rand_Rand(2) != 0) {
        NPCGlobals.nextPatrolTalkFrame = GameState.NumFramesUnpaused + FRAME_RATE_INT * 5;
        return false;
    }
    if (drone->skinClass != SKINCLASS_BLACK_OPS)
        return false;
    uint sfx = DroneFunc_ChooseSFX(DRONE_SFX_CHR_SPEECH_M5_BO1_009, DRONE_SFX_CHR_SPEECH_M5_BO1_014);
    NPCGlobals.nextPatrolTalkFrame = GameState.NumFramesUnpaused + FRAME_RATE_INT * 10;
    NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    return true;
}

// The Powerstation loudspeakers: while SWITCH_ENEMY_ALERTED_NEAR is on, an active drone that is not talking says the
// line of each channel that has come on, except the last one said. Always false.
// AUTOINJECT
bool NDrone2_PowerstationTalk(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (Drone_IsTalking(drone))
        return false;
    if (MPDrone_IsDyingOrDead(drone->gameObj) || !switch_channels[SWITCH_ENEMY_ALERTED_NEAR])
        return false;
    bool a1 = GameState.CurrentLevelHashcode == HT_Level_PowerStationA1;
    if (!a1 && GameState.CurrentLevelHashcode != HT_Level_PowerStationA2)
        return false;
    if (PowerstationLines[0].sfx == 0)
        return false;

    const PowerstationLine *line = PowerstationLines;
    do {
        uint channel = a1 ? line->channelA1 : line->channelA2;
        if (channel != 0 && channel != NPCGlobals.lastPowerstationChannel && switch_channels[channel] &&
            NDrone2_SpeechSFX(dcv, line->sfx, 0, 0, 0) != 0)
            NPCGlobals.lastPowerstationChannel = channel;
        line++;
    } while (line->sfx != 0);
    return false;
}

// Each frame, per drone: the Powerstation loudspeakers, or in CastleIndoors2 the guards' (skin class 12) talk to a
// player within 3 m they can see, tried every 30 frames at most: three lines in turn (5 s after the first, 15 s
// after the others), or once SWITCH_CASTLEINDOORS2_ZOE_LAST_LINE is on, a last one that ends it.
void NDrone2_HandleTalking(DCVars_tag *dcv) {
    if (Drone_bDisableSystem)
        return;
    Drone_tag *drone = dcv->drone;
    obj_tag *player = NDrone2_Player();

    if (GameState.CurrentLevelHashcode != HT_Level_CastleIndoors2) {
        if (GameState.CurrentLevelHashcode == HT_Level_PowerStationA1 ||
            GameState.CurrentLevelHashcode == HT_Level_PowerStationA2)
            NDrone2_PowerstationTalk(dcv);
        return;
    }

    if (drone->skinClass != SKINCLASS_ZOE || player == NULL)
        return;
    if (!(Vec_SqDist2D(&player->position, &dcv->gameObj->position) < 9.0f))
        return;
    if (drone->talkStep == 99 || drone->nextTalkFrame > GameState.NumFramesUnpaused)
        return;
    if (HandleTalkingDelay-- > 0)
        return;
    HandleTalkingDelay = 30;
    if (!NDrone2_CanSeeObject(drone, player, 5, 0))
        return;

    if (switch_channels[SWITCH_CASTLEINDOORS2_ZOE_LAST_LINE]) {
        NDrone2_SpeechSFX(dcv, DRONE_SFX_CHR_SPEECH_CAS_IG_ZOE08, 1, 0, 0);
        drone->talkStep = 99;
        return;
    }
    switch (drone->talkStep) {
    case 0:
        NDrone2_SpeechSFX(dcv, DRONE_SFX_CHR_SPEECH_M1_RESCUE_ZOE7, 1, 1, 0);
        drone->talkStep++;
        drone->nextTalkFrame = GameState.NumFramesUnpaused + FRAME_RATE_INT * 5;
        break;
    case 1:
        NDrone2_SpeechSFX(dcv, DRONE_SFX_CHR_SPEECH_CAS_IG_ZOE6B2, 1, 1, 0);
        drone->talkStep++;
        drone->nextTalkFrame = GameState.NumFramesUnpaused + FRAME_RATE_INT * 15;
        break;
    case 2:
        NDrone2_SpeechSFX(dcv, DRONE_SFX_CHR_SPEECH_CAS_IG_ZOE06B, 1, 1, 0);
        drone->talkStep--;
        drone->nextTalkFrame = GameState.NumFramesUnpaused + FRAME_RATE_INT * 15;
        break;
    }
}

// A Powerstation guard (skin class 5 or 6) says it has seen its opponent, one of 0xd6 and 0xd7, and its target is
// marked seen where the opponent was. Not with no player, in multiplayer, while it talks or once dead.
// AUTOINJECT
bool DroneFunc_NewSightTalk(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (NDrone2_Player() == NULL || MPSettings.dronesAreBots != 0)
        return false;
    if (Drone_IsTalking(drone))
        return false;
    if (drone->health <= 0.0f)
        return false;
    if ((ushort)drone->skinClass < SKINCLASS_SNIPER || (ushort)drone->skinClass > SKINCLASS_ELITE)
        return false;
    if (GameState.CurrentLevelHashcode != HT_Level_PowerStationA1 &&
        GameState.CurrentLevelHashcode != HT_Level_PowerStationA2)
        return false;
    uint sfx = DroneFunc_ChooseSFX(DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_10B, DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_1B);
    if (sfx == 0)
        return false;
    NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    DroneFunc_TargetSeen(dcv->drone, &dcv->drone->opponentSeenPos);
    return true;
}

// The same guards, in the player's cel, say they have lost sight of him: one of 0xd3 to 0xd5. The player is not
// checked for NULL.
// AUTOINJECT
bool DroneFunc_LostSightTalk(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (MPSettings.dronesAreBots != 0)
        return false;
    if (Drone_IsTalking(drone))
        return false;
    if (drone->health <= 0.0f)
        return false;
    if ((ushort)drone->skinClass < SKINCLASS_SNIPER || (ushort)drone->skinClass > SKINCLASS_ELITE)
        return false;
    if (GameState.CurrentLevelHashcode != HT_Level_PowerStationA1 &&
        GameState.CurrentLevelHashcode != HT_Level_PowerStationA2)
        return false;
    obj_tag *player = NDrone2_Player();
    if (drone->gameObj->inCel != player->inCel)
        return false;
    uint sfx = DroneFunc_ChooseSFX(DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_2B, DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_4B);
    if (sfx == 0)
        return false;
    NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    DroneFunc_TargetSeen(dcv->drone, &dcv->drone->opponentSeenPos);
    return true;
}

// Plays a Drone_SfxInfo sound at pos (or at the drone), its range scaled, at a pitch drawn from the entry's: not in
// the first frames after a load, nor with `check` within the entry's interval. Each try is logged in
// NPCGlobals.sfxLog and the frame kept in sfxFrame. A number past the table is read through NULL.
// AUTOINJECT
DYNAMICSOUNDS* NDrone2_PlaySFX(DCVars_tag *dcv, uint sfx, _VECTOR *pos, char check, float scale) {
    DroneSfxInfo *info = SfxInfo(sfx);
    if (info->sfx == 0)
        return NULL;
    uint now = GameState.NumFramesUnpaused;
    if (TooSoonAfterLoad(now))
        return NULL;
    if (check && PlayedTooRecently(info, now))
        return NULL;

    short pitch = 0;
    if (info->pitchMax != 0 || info->pitchMin != 0)
        pitch = (short)(Rand_Rand(info->pitchMax - info->pitchMin) + info->pitchMin);
    info->lastFrame = now;

    _VECTOR at;
    Vec_Zero(&at);
    if (pos != NULL)
        Vec_Copy(pos, &at);
    else if (dcv != NULL)
        Vec_Copy(&dcv->gameObj->position, &at);

    DYNAMICSOUNDS *handle = Sound_Play3D((Action_SFX)info->sfx, &at, info->volume, scale * info->radiusOuter,
                                         scale * info->radiusInner, pitch, dcv->aiStateMachine->id, 0);
    uint logged = 0;
    if (handle != NULL) {
        logged = sfx;
        info->lastFrame = now;
    }
    dcv->drone->sfxFrame = now;
    NPCGlobals.sfxLog[NPCGlobals.sfxLogNext].sfx = logged;
    NPCGlobals.sfxLog[NPCGlobals.sfxLogNext].handle = handle;
    if (++NPCGlobals.sfxLogNext >= DRONE_SFX_LOG_SIZE)
        NPCGlobals.sfxLogNext = 0;
    return handle;
}

// A line spoken, never in multiplayer, nor in the first frames after a load or within its interval. With `face`, an
// entry with a facial script (still loaded) plays that instead, unless one is playing; with needFace, so does one
// without, which then says nothing. Otherwise the sound, unless the drone is talking (and not `force`): it becomes
// speechChannel. Returns the sound, or NULL.
// AUTOINJECT
DYNAMICSOUNDS* NDrone2_SpeechSFX(DCVars_tag *dcv, uint sfx, char force, char face, char needFace) {
    if (MPSettings.dronesAreBots != 0)
        return NULL;
    Drone_tag *drone = dcv->drone;
    uint now = GameState.NumFramesUnpaused;
    if (TooSoonAfterLoad(now))
        return NULL;
    DroneSfxInfo *info = SfxInfo(sfx);
    if (PlayedTooRecently(info, now))
        return NULL;

    if (face && (info->facialAnim != 0 || needFace)) {
        if (info->facialAnim == 0 || hashtable_getitem(info->facialAnim) != NULL) {
            if (info->facialAnim != 0 && !dcv->drone->facialAnimPlaying && !dcv->drone->unknown37)
                NDrone2_PlayFacialAnim(dcv, info->facialAnim);
            if (drone->facialAnim != NULL)
                info->lastFrame = now;
            return NULL;
        }
        // The script is no longer loaded: the entry goes on without it
        info->facialAnim = (HASHCODE)0;
    }

    if (info->sfx == 0 || info->facialAnim != 0)
        return NULL;
    if (!force && drone->speechChannel != NULL)
        return NULL;
    drone->speechChannel = NDrone2_PlaySFX(dcv, sfx, &dcv->gameObj->position, 0, 2.0f);
    return drone->speechChannel;
}

// NDrone2_SpeechSFX for a drone rather than its DCVars. The DCVars are used whether Drone_DCVfromOBJ finds them or
// not (ours start zeroed where the original's are stack).
// AUTOINJECT
void NDrone2_SpeechSFXd(Drone_tag *drone, uint sfx, char force, char face, char needFace) {
    DCVars_tag dcv = {};
    Drone_DCVfromOBJ(drone->gameObj, &dcv);
    NDrone2_SpeechSFX(&dcv, sfx, force, face, needFace);
}
