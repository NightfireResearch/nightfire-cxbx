#pragma fp_contract(off)

#include "Trigger.h"

#include "Grid.h"
#include "TriggerManager.h"
#include "../Scheduler.hpp"             // RegisterEvent
#include "../platform/RealMath.h"
#include "../platform/RealPrint.h"      // MEM_fill

// ---------------------------------------------------------------------------------------------------------------
// WTrigger's methods (0x000cf300-0x000cf520), ported from the listing: running a trigger's events, the direction
// test, and moving a trigger with the object it follows.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
#define RayShell_GetTriggerHittingRayShell ((ActiveRayShell *(*)())0x00071980)

namespace {

constexpr float kMinimumRayValue = 50.0f;
constexpr float kCylinderRadiusScale = 0.55f;   // 0x3f0ccccd

}  // namespace

// Fills the event data from the trigger and the ray shell hitting it, if any, and runs each of its events. A trigger
// that fires once is disabled, events or not.
// FUNC_AT(0x000cf300)
void WTrigger::FireEvents(bool flag, int index, CARP::Instance *instance) {
    // The original tests the address of the list's events, which is never null: a trigger without a list faults
    // reading the count, as it does here.
    TriggerEvent *event = events->Events();
    MEM_fill(&gEventDynamicData, 0, sizeof(EventDynamicData));
    gEventDynamicData.flag = flag;
    gEventDynamicData.unknown0c = -1;
    gEventDynamicData.index = index;
    gEventDynamicData.instance = instance;
    gEventDynamicData.position = *Bounds();
    gEventDynamicData.position.w = 1.0f;
    gEventDynamicData.trigger = this;

    ActiveRayShell *ray = RayShell_GetTriggerHittingRayShell();
    if (ray != NULL) {
        gEventDynamicData.hasRayShell = true;
        gEventDynamicData.rayStart = ray->start;
        gEventDynamicData.rayEnd = ray->end;
        gEventDynamicData.rayValue = ray->unknown1c < kMinimumRayValue ? kMinimumRayValue : ray->unknown1c;
    } else {
        gEventDynamicData.hasRayShell = false;
    }

    for (int i = 0; i < events->count; i++, event++)
        RegisterEvent::LookupEvent(event->type)(reinterpret_cast<uintptr_t>(event->Data()));
    if (flags & kOnce)
        flags &= ~kEnabled;
}

// Whether the segment runs along the trigger's forward axis (or across it).
// FUNC_AT(0x000cf440)
bool WTrigger::TestDirection(const Coord4 *segment) {
    alignas(16) Coord4 direction;
    VU0_v4sub(&segment[1], &segment[0], &direction);
    return !(v3dotprod(&forward, &direction) < 0.0f);   // a NaN passes
}

// Puts the trigger's bottom under the centre it follows (the rotation is not used), turns a box into a cylinder,
// and moves it in the grid.
// FUNC_AT(0x000cf490)
bool WTrigger::UpdateRotPos(const Coord4 *rotation, const Coord3 *centre) {
    (void)rotation;
    Coord4 last = *Bounds();
    position.x = centre->x;
    position.y = float(centre->y - height * 0.5);
    position.z = centre->z;
    if (shape == kBox) {
        shape = kCylinder;
        if (height > radius)
            radius = height * kCylinderRadiusScale;
    }
    WGrid::AddGridNodeDynamicElement(&last, Bounds(), kGridTrigger, uint32_t(this - fgTriggerManager->triggers));
    return true;
}
