#pragma once

#include <stdint.h>

// The few calls into EA's EAGL renderer that our rendering code makes so far, each calling the original. None of
// these classes has its fields mapped; they are only somewhere for the methods to live, under Ghidra's names.

struct MATRIX4;

// The renderer's device-level state (Ghidra: EAGL::RenderContext); the one in use is RRenderer::fgRenderer's, at +0x64.
class RenderContext {
public:
    // Turns depth writes on or off (0x000e73f0).
    // AUTOGEN
    uint8_t SetZWritesEnable(uint8_t enable);
};

// A primitive's render state (Ghidra: EAGL::GeoPrimState).
class GeoPrimState {
public:
    // The depth comparison, in OpenGL's numbering: 0x201 GL_LESS (the default), 0x207 GL_ALWAYS (0x000eecd0).
    // AUTOGEN
    void SetDepthTestMethod(int method);
};

// A material whose vertices are rebuilt every frame (Ghidra: UVolatileMaterial). The caller points the current
// slot of the draw-request ring (below) at its vertex arrays, then calls Draw.
class UVolatileMaterial {
public:
    // Queues `count` vertices of primitive type `primitive` (8 for the glares' quads) with this material and an
    // optional transform (none: identity), and moves the ring on to its next slot (0x0011c290).
    // AUTOGEN
    void Draw(int primitive, int count, MATRIX4 *transform);
};

// The ring of 16 draw requests UVolatileMaterial::Draw fills: the index of the current one, and the requests.
// Fields of a request, as the glares use them: +0x10 the texture, +0x28 the positions (float[4] each), +0x30 the
// colours (ARGB), +0x38 the texture coordinates (float[2]); Draw itself writes the counts and the material.
#define VolatileRequestIndex (*(int *)0x00243830)
#define VolatileRequests ((uint8_t **)0x00243878)
