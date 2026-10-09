#ifndef DRIVING_EAGL_GEOPRIMSTATE_H_
#define DRIVING_EAGL_GEOPRIMSTATE_H_

// EAGL::GeoPrimState, the per-primitive render state, and its Xbox extension's setters (docs/driving/eagl.md 2.5,
// 4.5). See GeoPrimState.cpp. In namespace EAGL, as in Ghidra.

#include <stdint.h>

namespace EAGL {

struct GeoPrimStateExtension;

struct GeoPrimState {                // 0x4c; the game derives its materials from it
    uint32_t primitiveType;          // +0x00
    uint32_t shading;                // +0x04 0 flat, 1 Gouraud, 2 Gouraud with specular (D3DRS_SHADEMODE)
    uint8_t cullEnable;              // +0x08
    uint8_t pad09[3];
    uint32_t cullDirection;          // +0x0c (Xbox extension) the D3D cull mode when culling
    uint32_t depthTestMethod;        // +0x10 OpenGL numbering: 0x201 LESS ... 0x207 ALWAYS
    uint32_t alphaBlendMode;         // +0x14 0..5, picks blendOperation/source/destination
    uint8_t alphaTestEnable;         // +0x18
    uint8_t pad19[3];
    uint32_t alphaCompareValue;      // +0x1c
    uint32_t alphaTestMethod;        // +0x20
    uint8_t textureEnable;           // +0x24
    uint8_t pad25[3];
    uint32_t transparencyMethod;     // +0x28 0 or 1: alpha blending on (D3DRS_ALPHABLENDENABLE)
    uint32_t fillMode;               // +0x2c (Xbox extension)
    uint32_t blendOperation;         // +0x30 0x8006 ADD, 0x800b REVERSE_SUBTRACT
    uint32_t blendSource;            // +0x34
    uint32_t blendDestination;       // +0x38
    float zSlopeScale;               // +0x3c
    float zOffset;                   // +0x40 0: polygon offset off
    uint32_t blendColour;            // +0x44
    uint32_t zWritesEnable;          // +0x48 0, 1, or -1 for the render context's own

    GeoPrimState* Construct();                                               // 0x000ef480
    GeoPrimState* ConstructCopy(const GeoPrimState *other);                  // 0x000ef4a0
    void Destruct();                                                         // 0x000ef490
    bool Apply();                                                            // 0x000ef050 (invented)

    bool SetPrimitiveType(uint32_t type);                                    // 0x000eec70
    bool GetPrimitiveType(uint32_t *type) const;                             // 0x000eec80
    bool SetShading(uint32_t shading);                                       // 0x000eec90 (Ghidra: SetTransparencyMethod)
    bool GetShading(uint32_t *shading) const;                                // 0x000eeca0
    bool SetCullEnable(bool enable);                                         // 0x000eecb0
    bool GetCullEnable(bool *enable) const;                                  // 0x000eecc0
    bool SetDepthTestMethod(uint32_t method);                                // 0x000eecd0
    bool GetDepthTestMethod(uint32_t *method) const;                         // 0x000eece0
    bool SetAlphaBlendMode(uint32_t mode);                                   // 0x000eecf0
    bool GetAlphaBlendMode(uint32_t *mode) const;                            // 0x000eedb0
    bool SetAlphaTestEnable(bool enable);                                    // 0x000eedc0
    bool GetAlphaTestEnable(bool *enable) const;                             // 0x000eedd0
    bool SetAlphaCompareValue(uint32_t value);                               // 0x000eede0
    bool GetAlphaCompareValue(uint32_t *value) const;                        // 0x000eedf0
    bool SetAlphaTestMethod(uint32_t method);                                // 0x000eee00
    bool GetAlphaTestMethod(uint32_t *method) const;                         // 0x000eee10
    bool SetTextureEnable(bool enable);                                      // 0x000eee20
    bool GetTextureEnable(bool *enable) const;                               // 0x000eee30
    bool SetTextureCoordType(uint32_t type);                                 // 0x000eee40 (not kept: false)
    bool GetTextureCoordType(uint32_t *type) const;                          // 0x000eee50 (false)
    bool SetTransparencyMethod(uint32_t method);                             // 0x000eee60
    bool GetTransparencyMethod(uint32_t *method) const;                      // 0x000eee70
    bool SetChromaColour(uint32_t colour);                                   // 0x000eee80 (not kept: false)
    bool GetChromaColour(uint32_t *colour) const;                            // 0x000eee90 (false)

    // The Xbox extension, this object
    GeoPrimStateExtension* Extension();
};
static_assert(sizeof(GeoPrimState) == 0x4c, "a GeoPrimState is 0x4c bytes");

// The Xbox extension's setters work on the same object (its extension pointer is the object itself).
struct GeoPrimStateExtension : GeoPrimState {
    GeoPrimStateExtension* Construct();                                      // 0x000eeea0
    void DumpState();                                                        // 0x000eef10 (empty)
    bool SetCullDirection(uint32_t direction);                               // 0x000eef20
    bool GetCullDirection(uint32_t *direction) const;                        // 0x000eef30
    bool SetFillMode(uint32_t mode);                                         // 0x000eef40
    bool GetFillMode(uint32_t *mode) const;                                  // 0x000eef50
    bool SetBlendOperation(uint32_t operation);                              // 0x000eef60
    bool GetBlendOperation(uint32_t *operation) const;                       // 0x000eef70
    bool SetAlphaBlend(uint32_t source, uint32_t destination, uint32_t operation);           // 0x000eef80
    bool GetAlphaBlend(uint32_t *source, uint32_t *destination, uint32_t *operation) const;  // 0x000eefa0
    bool SetZOffset(float offset);                                           // 0x000eefc0
    bool GetZOffset(float *offset) const;                                    // 0x000eefd0
    bool SetZSlopeScale(float scale);                                        // 0x000eefe0
    bool GetZSlopeScale(float *scale) const;                                 // 0x000eeff0
    bool SetBlendColour(uint32_t colour);                                    // 0x000ef000
    bool GetBlendColour(uint32_t *colour) const;                             // 0x000ef010
    bool SetZWritesEnable(bool enable);                                      // 0x000ef020
    bool GetZWritesEnable(bool *enable) const;                               // 0x000ef030
};
inline GeoPrimStateExtension* GeoPrimState::Extension() { return static_cast<GeoPrimStateExtension *>(this); }

}  // namespace EAGL

#endif // DRIVING_EAGL_GEOPRIMSTATE_H_
