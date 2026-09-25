#include "RGlareManager.hpp"
#include "RenderState.hpp"
#include "VectorMaths.hpp"
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
//
// UNINJECTABLE - takes its glare in ESI, and its only caller (AddModelGlare) is replaced
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

// ---------------------------------------------------------------------------------------------------------------
// Drawing: DrawGlares and its two quad builders.
//
// The vertices go into four fixed arrays of the game's, sized for the worst case - 128 glares, two sprites each,
// four vertices a sprite, 1024 in all - and are handed to the draw-request ring with the glare material. The
// constructor set that material up (quads, blended, the "flar" texture), which is why nothing here sets it.
// ---------------------------------------------------------------------------------------------------------------

#define GlarePositions   ((float (*)[4])0x00202cb0)
#define GlareUVs         ((float (*)[2])0x00206cb0)
#define GlareTexture     (*(uint32_t *)0x00208cb0)     // the "flar" texture, from the constructor
#define GlareColours     ((uint32_t *)0x00208cc0)
// The glare material. Its depth state is set through the same object, as the GeoPrimState it starts with.
#define GlareMaterial    ((UVolatileMaterial *)0x00209cc0)
#define GlareDepthState  ((GeoPrimState *)0x00209cc0)
#define GlareDrawParity  (*(int *)0x00209d28)          // flipped by every DrawGlares with glares; never read

// Degrees in a radian, as the float the original multiplies by.
static const float kDegreesPerRadian = 57.295784f;

// A sprite's four corners, in the order both builders emit them: texture coordinates, and the multiples of the
// two spun axes that place the corner. The binary's tables (0x001c8e4c, 0x001c8e6c, 0x001c8e90, 0x001c8eb0).
static const float kCornerUV[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
static const float kWorldCorner[4][2] = { { 1, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 } };    // (up, right)
static const float kScreenCorner[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };   // (up, right)

// The camera the glares face: its axes at +0x10, +0x20 and +0x30 (the last the one they spin about) and its
// position at +0x40. RRenderer::fgRenderer->[+4] is the view; the view's +4 is this.
static uint8_t *GlareView() {
    return *(uint8_t **)(RRendererInstance + 4);
}
static uint8_t *GlareCamera() {
    return *(uint8_t **)(GlareView() + 4);
}

// One vertex into the arrays. The original computes each coordinate on the x87, so in extended precision, and
// rounds once when it stores; double is the nearest C++ has, and with coefficients of +-1 the sums are exact in
// it anyway.
static void EmitGlareVertex(int *count, int corner, double x, double y, double z, uint32_t colour,
                            const GlareSprite *sprite) {
    int i = *count;
    GlarePositions[i][0] = (float)x;
    GlarePositions[i][1] = (float)y;
    GlarePositions[i][2] = (float)z;
    GlarePositions[i][3] = 1.0f;
    GlareColours[i] = colour;
    GlareUVs[i][0] = (float)(kCornerUV[corner][0] * (double)sprite->uvSize + sprite->u);
    GlareUVs[i][1] = (float)(kCornerUV[corner][1] * (double)sprite->uvSize + sprite->v);
    *count = i + 1;
}

// AUTOINJECT
void __stdcall AddGlareToRender(int *count, const Glare *glare, const GlareSprite *sprite, float size,
                                uint32_t colour, float distance, float zBias) {
    const uint8_t *camera = GlareCamera();
    const float *spinAxis = (const float *)(camera + 0x30);

    // The camera's two screen axes, spun about its view axis and scaled to the sprite's size.
    alignas(16) MATRIX4 spin;
    BuildRotate(&spin, (float)((double)distance * sprite->spin * kDegreesPerRadian), spinAxis[0], spinAxis[1],
                spinAxis[2]);
    Vec4 up, right;
    VU0_MATRIX4_vect3mult(camera + 0x20, &spin, &up);
    VU0_MATRIX4_vect3mult(camera + 0x10, &spin, &right);
    VEC4_Scale(&up, size, &up);
    VEC4_Scale(&right, size, &right);

    // The centre, moved size * zBias towards the camera.
    Vec4 centre = { glare->position.x, glare->position.y, glare->position.z, 0.0f };
    Vec4 away;
    VEC4_Subtract(&centre, camera + 0x40, &away);
    VU0_v4unitxyz(&away, &away);
    VEC4_ScaleAdd(&away, (float)-((double)size * zBias), &centre, &centre);

    for (int corner = 0; corner < 4; corner++) {
        double u = kWorldCorner[corner][0], r = kWorldCorner[corner][1];
        EmitGlareVertex(count, corner, r * right.x + u * up.x + centre.x, r * right.y + u * up.y + centre.y,
                        r * right.z + u * up.z + centre.z, colour, sprite);
    }
}

// AUTOINJECT
void __stdcall Add2DGlareToRender(int *count, const Glare *glare, const GlareSprite *sprite, float size,
                                  uint32_t colour, float distance) {
    size = (float)((double)size * 600.0);

    // The screen's axes, spun about z and scaled to the sprite's size.
    alignas(16) MATRIX4 spin;
    BuildRotate(&spin, (float)((double)distance * sprite->spin * kDegreesPerRadian), 0.0f, 0.0f, 1.0f);
    const Vec4 xAxis = { 1.0f, 0.0f, 0.0f, 0.0f };
    const Vec4 yAxis = { 0.0f, 1.0f, 0.0f, 0.0f };
    Vec4 up, right;
    VU0_MATRIX4_vect3mult(&yAxis, &spin, &up);
    VU0_MATRIX4_vect3mult(&xAxis, &spin, &right);
    VEC4_Scale(&up, size, &up);
    VEC4_Scale(&right, size, &right);

    // At the glare's x and y, and a fixed depth of 0.2.
    for (int corner = 0; corner < 4; corner++) {
        double u = kScreenCorner[corner][0], r = kScreenCorner[corner][1];
        EmitGlareVertex(count, corner, r * right.x + u * up.x + glare->position.x,
                        r * right.y + u * up.y + glare->position.y, 0.2f, colour, sprite);
    }
}

// A directional glare's colour, faded: red, green and blue scaled by its intensity and capped at 255, alpha kept.
// Put together as the original does - each channel truncated and ORed in below the ones already there - so a
// value outside 0..255 would spill into its neighbours the same way (none does: the intensity is positive).
static uint32_t FadeGlareColour(uint32_t colour, float intensity) {
    float channel[3];
    for (int i = 0; i < 3; i++) {
        channel[i] = (float)((double)((colour >> (16 - 8 * i)) & 0xff) * intensity);
        if (channel[i] > 255.0f)
            channel[i] = 255.0f;
    }
    uint32_t packed = (colour >> 24) << 8;
    packed |= (uint32_t)(int64_t)channel[0];
    packed <<= 8;
    packed |= (uint32_t)(int64_t)channel[1];
    packed <<= 8;
    return packed | (uint32_t)(int64_t)channel[2];
}

// AUTOINJECT
void RGlareManager::DrawGlares(bool inWorld) {
    if (glareCount == 0)
        return;

    const uint8_t *view = GlareView();
    const uint8_t *camera = GlareCamera();
    farDepth = -1e8f;
    screenScale = (float)(66.0 / ((double)*(const float *)(camera + 0xb4) * *(const float *)(view + 0x48)));

    int vertices = 0;
    for (int i = 0; i < glareCount; i++) {
        const Glare *glare = &glares[i];
        float distance = vec3distance(glare, camera + 0x40);
        const GlareType *type = &types[glare->type];
        const GlareSprite *sprites[2] = { &type->halo, &type->spike };
        const uint32_t colours[2] = { type->haloColour, type->spikeColour };
        const float sizes[2] = { type->haloSize, type->spikeSize };

        for (int layer = 0; layer < 2; layer++) {
            uint32_t colour = colours[layer];
            if (glare->flags & GLARE_DIRECTIONAL)
                colour = FadeGlareColour(colour, glare->rangeOrIntensity);
            float size = (float)((double)sizes[layer] * glare->rangeOrIntensity);
            if (inWorld)
                AddGlareToRender(&vertices, glare, sprites[layer], size, colour, distance, type->zBias);
            else
                Add2DGlareToRender(&vertices, glare, sprites[layer], size, colour, distance);
        }
    }

    if (vertices > 0) {
        (*(RenderContext **)(RRendererInstance + 0x64))->SetZWritesEnable(0);
        if (!inWorld)
            GlareDepthState->SetDepthTestMethod(0x207);   // GL_ALWAYS

        uint8_t *request = VolatileRequests[VolatileRequestIndex];
        *(uint32_t **)(request + 0x30) = GlareColours;
        *(float (**)[4])(request + 0x28) = GlarePositions;
        *(float (**)[2])(request + 0x38) = GlareUVs;
        *(uint32_t *)(request + 0x10) = GlareTexture;
        GlareMaterial->Draw(8, vertices, nullptr);

        if (!inWorld)
            GlareDepthState->SetDepthTestMethod(0x201);   // GL_LESS
        (*(RenderContext **)(RRendererInstance + 0x64))->SetZWritesEnable(1);
        glareCount = 0;
    }
    GlareDrawParity = 1 - GlareDrawParity;
}
