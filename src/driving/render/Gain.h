#ifndef DRIVING_RENDER_GAIN_H_
#define DRIVING_RENDER_GAIN_H_

// ---------------------------------------------------------------------------------------------------------------
// RGain (0x12c bytes, one instance at 0x00200f20, a USingleton): a gain and an offset applied to the whole screen
// - the post-processing's subsampled back buffer drawn over it as one quad through a game-built GeoPrim - unless
// they are the identity. See Gain.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../eagl/GeoPrimState.h"
#include "../eagl/Model.h"              // DynamicModel
#include "../eagl/RenderMethod.h"       // GeoPrimParam

// The gain's GeoPrim (0x54), built by Construct over a child of the render method at 0x00242e50:
// parameter 4 is the model-view-projection matrix; RGain fills 0 the render state, 1 the texture, 2 the vertex
// count record, 5 the gain, 6 the offset, 7 positions, 8 texture coordinates, 9 colours. The name is ours.
struct GainGeoPrim {
    EAGL::RenderMethod *method;         // +0x00
    EAGL::GeoPrimParam params[10];      // +0x04

    GainGeoPrim* Construct();                                                           // 0x0009dd40
};
static_assert(sizeof(GainGeoPrim) == 0x54, "the gain's GeoPrim is 0x54 bytes");

class RGain {
public:
    const void *vtable;                 // +0x00 0x00192bdc
    float gain[4];                      // +0x04
    float offset[4];                    // +0x14
    EAGL::GeoPrimState state;           // +0x24
    EAGL::DynamicModel model;           // +0x70 holds geoPrim
    uint32_t vertexCount[4];            // +0xc8 the vertex count record: the count, then zeros
    GainGeoPrim geoPrim;                // +0xd8

    RGain* Construct();                                                                 // 0x0009dfe0
    void Destruct();                                                                    // 0x0009de00
    RGain* Delete(unsigned flags);          // the scalar deleting destructor, vtable slot 0  // 0x0009e120
    static void Kill();                     // vtable slot 2                             // 0x0009de80
    // The identity: gain 1, offset 0; vtable slot 1
    void Reset();                                                                       // 0x0009dea0
    void Draw();                                                                        // 0x0009dec0
};
static_assert(sizeof(RGain) == 0x12c, "RGain is 0x12c bytes");
static_assert(offsetof(RGain, state) == 0x24 && offsetof(RGain, model) == 0x70 &&
              offsetof(RGain, vertexCount) == 0xc8 && offsetof(RGain, geoPrim) == 0xd8, "RGain layout");

#define TheGain (*(RGain **)0x00200f20)

#endif // DRIVING_RENDER_GAIN_H_
