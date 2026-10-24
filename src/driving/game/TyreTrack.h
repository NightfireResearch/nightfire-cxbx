#ifndef DRIVING_GAME_TYRETRACK_H_
#define DRIVING_GAME_TYRETRACK_H_

// ---------------------------------------------------------------------------------------------------------------
// RTyreTrack (0x920 bytes, "RTyreTrack"; PBondCar makes one per wheel): the track a wheel leaves, a ring of up to 32
// (64 for a long track) points, each a pair of edge points across the wheel's path on the ground and a colour by
// the surface. Add puts a new point every 4 or 8 calls (the wheel's previous point otherwise moved), Break ends the
// strip, and Draw draws the ring as triangle strips through four volatile materials in turn, with the tyre or the
// snow texture. See TyreTrack.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../render/Materials.h"        // UVolatileMaterial

namespace EAGL {
struct TAR;
}
class WWorldPos;

class RTyreTrack {
public:
    static constexpr int kMaxPoints = 64;
    static constexpr uint8_t kShortTrack = 32;
    static constexpr uint8_t kLongTrack = 64;

    // The ground kinds (WWorldPos's face tag type) Add and Break colour by
    enum Surface {
        kSurface3 = 3,
        kSurface8 = 8,
        kSurface9 = 9,
    };

    // One point of the track: the two edges across the wheel's path
    struct Point {
        Coord4 left;                    // w 1
        Coord3 right;
        uint32_t joined;                // 1: the strip runs on from the previous point; 0: it starts again here
    };

    Point points[kMaxPoints];           // +0x000 a ring of `capacity`
    uint32_t colours[kMaxPoints];       // +0x800 0xAARRGGBB of each point
    int32_t count;                      // +0x900 the points in the ring
    int32_t head;                       // +0x904 the oldest, once the ring is full
    int32_t current;                    // +0x908 the point Add last wrote
    int32_t drawn;                      // +0x90c the points Draw draws
    float width;                        // +0x910 the edges' distance apart
    uint8_t segments;                   // +0x914 Adds since the last Break
    uint8_t advanced;                   // +0x915 the last Add or Break moved on to a new point
    uint8_t capacity;                   // +0x916 kShortTrack or kLongTrack
    uint8_t snow;                       // +0x917 drawn subtracted, with the snow textures
    uint8_t unknown918[8];

    RTyreTrack* Construct(float width, bool longTrack, bool snow);                              // 0x000abfc0
    // The textures, the strips' texture coordinates and the four materials
    static void Init();                                                                         // 0x000ac050
    void Break(int surface);                                                                    // 0x000ac170
    void Draw();                                                                                // 0x000ac250
    // A point at `point` on `ground`. `slip` and `spin` (the caller's wheel slip and the car's turn rate, 1 on a
    // snowmobile) make its alpha; a spin above 0.75 makes points closer together.
    void Add(const Coord4 *point, float slip, float spin, int surface, WWorldPos *ground);      // 0x000ac4c0

private:
    // The point before `index` in the ring
    int Previous(int index) const { return index > 0 ? index - 1 : capacity - 1; }
};
static_assert(sizeof(RTyreTrack) == 0x920, "RTyreTrack is 0x920 bytes");
static_assert(offsetof(RTyreTrack, colours) == 0x800 && offsetof(RTyreTrack, count) == 0x900 &&
              offsetof(RTyreTrack, width) == 0x910 && offsetof(RTyreTrack, capacity) == 0x916 &&
              offsetof(RTyreTrack, snow) == 0x917, "RTyreTrack layout");

// The strips' vertex data (each draw points the material's request at it), the textures, the four materials (the
// static initialisers construct them) and the next of them
struct TrackTexCoord {
    float u, v;
};
constexpr int kTrackBatchVertices = 0x200;      // the most vertices one draw sends
constexpr int kTrackMaterialCount = 4;
extern TrackTexCoord TrackTexCoords[kTrackBatchVertices];
extern Coord4 TrackVertices[kTrackBatchVertices];
extern uint32_t TrackColours[kTrackBatchVertices];
extern UVolatileMaterial TrackMaterials[kTrackMaterialCount];
extern EAGL::TAR *TrackTextures[3];             // "skid", "sno1", "sno2"
extern uint32_t TrackMaterialIndex;

#endif // DRIVING_GAME_TYRETRACK_H_
