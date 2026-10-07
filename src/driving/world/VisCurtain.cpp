#include "VisCurtain.h"

#include "../../helpers.h"
#include "../platform/RealMath.h"

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// The visibility curtains (0x000d0d50-0x000d1060), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

#define ActiveCurtainCount I32_AT(0x0023e280)
#define ActiveCurtains ((MATRIX4 *)0x0023e290)        // 64 of them; kMaxActiveCurtains used

// FUNC_AT(0x000d0d50)
void InitActiveCurtainList() {
    ActiveCurtainCount = 0;
}

// FUNC_AT(0x000d0d60)
void AddActiveCurtain(const WVisCurtain *curtain, const Coord4 *eye) {
    if (ActiveCurtainCount >= kMaxActiveCurtains)
        return;
    MATRIX4 *planes = &ActiveCurtains[ActiveCurtainCount];

    // the wall's plane, facing the eye
    Coord4 up = { 0.0f, curtain->end.y - curtain->start.y, 0.0f, 0.0f };
    Coord4 across = { curtain->start.x - curtain->end.x, 0.0f, curtain->start.z - curtain->end.z, 0.0f };
    Coord4 wall = {};
    VU0_v4unitcrossprodxyz(&up, &across, &wall);
    wall.w = v3dotprod(&curtain->start, &wall);
    if (v3dotprod(eye, &wall) - wall.w < 0.0f)
        return;
    ActiveCurtainCount++;

    // the planes from the eye through each end, and through the far end along the wall
    Coord4 down = { 0.0f, curtain->start.y - curtain->end.y, 0.0f, 0.0f };
    Coord4 toStart = {}, toEnd = {};
    VU0_v4sub(eye, &curtain->start, &toStart);
    VU0_v4sub(eye, &curtain->end, &toEnd);
    Coord4 startSide = {}, endSide = {}, back = {};
    VU0_v4unitcrossprodxyz(&down, &toStart, &startSide);
    VU0_v4unitcrossprodxyz(&up, &toEnd, &endSide);
    VU0_v4unitcrossprodxyz(&across, &toEnd, &back);
    startSide.w = v3dotprod(&curtain->start, &startSide);
    endSide.w = v3dotprod(&curtain->end, &endSide);
    back.w = v3dotprod(&curtain->end, &back);

    const Coord4 *columns[4] = { &wall, &startSide, &endSide, &back };
    for (int column = 0; column < 4; column++) {
        planes->mtx[0][column] = columns[column]->x;
        planes->mtx[1][column] = columns[column]->y;
        planes->mtx[2][column] = columns[column]->z;
        planes->mtx[3][column] = columns[column]->w;
    }
}

// FUNC_AT(0x000d0f50)
bool IsVisibleAgainstCurtains(const Coord4 *sphere, float farMargin) {
    for (int i = 0; i < ActiveCurtainCount; i++) {
        Coord4 distance;
        VU0_MATRIX4_vect3multsub(sphere, &ActiveCurtains[i], &distance);
        float margin = -sphere->w;
        if (distance.x < margin && distance.y < margin && distance.z < margin && distance.w < -farMargin)
            return false;
    }
    return true;
}

// FUNC_AT(0x000d0ff0)
WVisCurtain* WVisCurtain::Construct(const Coord4 *from, const Coord4 *to) {
    start = *from;
    end = *to;
    Coord4 middle = {};
    VU0_v4addscale(&start, &end, 0.5f, &middle);
    start.w = VU0_v3distancexz(&start, &middle);
    return this;
}
