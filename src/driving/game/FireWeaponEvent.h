#ifndef DRIVING_GAME_FIREWEAPONEVENT_H_
#define DRIVING_GAME_FIREWEAPONEVENT_H_

// ---------------------------------------------------------------------------------------------------------------
// EFireWeapon: a shot from a weapon the CARP data or a holder fires - the player's car's, an AI car's, a
// helicopter's, a sentry's or a character's ActWeapon. It aims (along an axis of the muzzle's frame, the player's
// camera's aim, or the AI's target with its accuracy and some noise), spawns the weapon's shell, missile, mine,
// grenade or ray shell, plays the fire sound and the force feedback. See FireWeaponEvent.cpp and EventsB.h for the
// event model.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../EventManager.hpp"            // Event
#include "../../common/xbeClass.h"

class ActWeapon;
class RSceneObj;
struct ArticleEffect;
namespace CARP {
class Instance;
}

// (0x18 bytes; vtable 0x0018c520)
class EFireWeapon : public Event {
public:
    struct Record {
        uint32_t unknown00;
        ArticleEffect *effect;
    };

    ArticleEffect *effect;              // +0x04 the muzzle; NULL: the instance's frame
    RSceneObj *sceneObj;                // +0x08 what raised it
    CARP::Instance *instance;           // +0x0c ... (not read)
    uint32_t instanceIndex;             // +0x10 ... its instance
    ActWeapon *weapon;                  // +0x14 ... the holder's weapon; NULL: none

    void Destruct();                                                                    // 0x0004ac40
    EFireWeapon* Delete(unsigned flags);                                                // 0x0004ec40

private:
    void Process();
};
XBE_CLASS_SIZE(EFireWeapon, 0x18);

// The henchmen's gun sounds' counters (FireWeaponEvent.cpp; names ours)
extern int32_t gCarGunShots;
extern uint32_t gHenchGunPick;
extern uint32_t gHenchGunShots;

// ---- the MakeEvent callbacks (RegisterEvent::LookupEvent's): a new event from the record. Ghidra names them
// GetEventName; the names are the AUF GameCube build's.

void EFireWeapon_MakeEvent_Callback(const EFireWeapon::Record *record);                         // 0x00040090

#endif // DRIVING_GAME_FIREWEAPONEVENT_H_
