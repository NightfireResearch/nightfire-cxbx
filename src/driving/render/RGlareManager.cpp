#include "RGlareManager.hpp"
#include "../drivinghelpers.h"

#include <math.h>

// The game's tick, which the blink is timed against, and the renderer, whose camera the directional fade
// looks along. Addresses from AddModelGlare (0x000a9b7d) and GlareBlinkBrightness (0x000a99a1).
#define GlareTick        U32_AT(0x001f2a4c)
#define RRendererInstance (*(uint8_t **)0x001ebff4)

// The three maths helpers AddModelGlare calls, all __cdecl with stack arguments: a point through a matrix
// (0x00114e20, a wrapper of VU0_MATRIX4_vect3mult), a direction through its rotation (0x00114e60), and a dot
// product (0x001089e0). Not reimplemented; these reach the originals.
// AUTOGEN(0x00114e20)
void MATRIX4_TransformPoint(MATRIX4 *m, const Glare *in, Glare *out);
// AUTOGEN(0x00114e60)
void MATRIX4_RotateVector(MATRIX4 *m, const _VEC3 *in, _VEC3 *out);
// AUTOGEN(0x001089e0)
float VEC3_Dot(const _VEC3 *a, const _VEC3 *b);

// GlareBlinkBrightness (0x000a99a0). The original takes the glare in ESI - a register argument no C++
// convention can receive - and its only caller is AddModelGlare, so it is reimplemented here as an ordinary
// function rather than patched: with AddModelGlare replaced, the original is never reached. See "Does the
// driving engine need register-argument adaptors?" in docs/driving-injection-framework.md.
//
// The blink is a phase in [0, 1) that advances at blinkRate per tick from blinkStartTick. The glare is dark once
// the phase passes blinkDuty; before that its brightness is blinkBase + blinkAmplitude * shape(phase), where the
// shape is the phase itself (a sawtooth), a triangle wave, or a pulse that rises, dips at its peak and falls.
// The original computes in the x87's extended precision; double is the nearest C++ has.
static double GlareBlinkBrightness(const Glare *glare) {
    uint32_t elapsed = GlareTick - glare->blinkStartTick;
    float cycles = (float)elapsed * glare->blinkRate;
    double phase = (double)cycles - floor((double)cycles);
    if (phase > glare->blinkDuty)
        return 0.0;
    if (glare->flags & GLARE_BLINK_SHAPED) {
        if (glare->flags & GLARE_BLINK_PULSE) {
            double shape;
            if (phase <= 0.4)
                shape = phase * 2.5;
            else if (phase < 0.5)
                shape = 2.0 - (phase + 0.1 + phase + 0.1);
            else if (phase < 0.6)
                shape = (phase - 0.1) + (phase - 0.1);
            else
                shape = 1.0 - (phase - 0.6) * 2.5;
            return shape * glare->blinkAmplitude + glare->blinkBase;
        }
        phase = phase + phase;
        if (phase > 1.0)
            phase = 2.0 - phase;
    }
    return phase * glare->blinkAmplitude + glare->blinkBase;
}

// AUTOINJECT
void RGlareManager::AddGlare(Glare *glare, int unused) {
    (void)unused;
    if (glareCount >= 128 || !enabled)
        return;
    glares[glareCount] = *glare;
    glares[glareCount].w = 1.0f;
    glareCount++;
}

// AUTOINJECT
void RGlareManager::AddModelGlare(Glare *node, MATRIX4 *transform, float distance) {
    if (glareCount >= 128 || !enabled)
        return;

    double brightness = 1.0;
    if (node->flags & GLARE_BLINKS) {
        brightness = GlareBlinkBrightness(node);
        if (brightness < 0.0001)
            return;
    }
    // node->rangeOrIntensity is the node's range here; beyond 1.6 times it (scaled by the blink) the glare is
    // not drawn. The queued copy keeps the product as its intensity.
    double intensity = brightness * node->rangeOrIntensity;
    if (distance / intensity > 1.6)
        return;

    Glare *glare = &glares[glareCount];
    *glare = *node;
    glare->w = 1.0f;
    glare->rangeOrIntensity = (float)intensity;
    MATRIX4_TransformPoint(transform, node, glare);

    if (node->flags & GLARE_DIRECTIONAL) {
        // Brightest seen head-on, gone at ninety degrees: -(facing * intensity) - 0.5, doubled, where facing is
        // the dot product of the camera's forward vector and the glare's direction.
        MATRIX4_RotateVector(transform, &node->direction, &glare->direction);
        uint8_t *camera = *(uint8_t **)(RRendererInstance + 4);
        const _VEC3 *forward = (const _VEC3 *)(*(uint8_t **)(camera + 4) + 0x30);
        glare->rangeOrIntensity = -(VEC3_Dot(forward, &glare->direction) * glare->rangeOrIntensity);
        glare->rangeOrIntensity = glare->rangeOrIntensity - 0.5f;
        if (glare->rangeOrIntensity <= 0.0f)
            return;
        glare->rangeOrIntensity = glare->rangeOrIntensity + glare->rangeOrIntensity;
    }
    glareCount++;
}
