#pragma fp_contract(off)

#include "TyreTrack.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>

#include "../../helpers.h"
#include "../eagl/RenderContext.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"                // Ftol
#include "../render/Materials.h"            // UVolatileMaterial, VolatileRequests
#include "../render/Renderer.h"             // fgRenderer
#include "../render/TextureContext.h"
#include "../world/WorldPos.h"

// ---------------------------------------------------------------------------------------------------------------
// RTyreTrack (0x000abfc0-0x000ac870), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

namespace {

constexpr int kBatchVertices = kTrackBatchVertices;
constexpr int kTrackMaterials = kTrackMaterialCount;

enum TrackTexture {
    kTextureSkid,
    kTextureSnow1,
    kTextureSnow2,                              // a snow track narrower than kNarrowTrack
    kTrackTextures
};
static_assert(kTrackTextures == sizeof(TrackTextures) / sizeof(TrackTextures[0]), "TrackTextures");
constexpr uint32_t kTextureNames[kTrackTextures] = {
    0x64696b73,                                 // "skid"
    0x316f6e73,                                 // "sno1"
    0x326f6e73,                                 // "sno2"
};

// The points' colours (alpha in the top byte)
constexpr uint32_t kColourDefault = 0x080808;
constexpr uint32_t kColourSurface3 = 0x381a00;
constexpr uint32_t kColourGrey = 0x404040;     // a snow track's, and kSurface9's
constexpr uint32_t kColourWhite = 0xffffff;    // kSurface8's

constexpr uint32_t kBlendSubtract = 5;          // GeoPrimState::SetAlphaBlendMode's
constexpr float kNarrowTrack = 0.5f;
constexpr float kSnowSpacing = 0.5f;            // a snow track's least distance between points, in x and z
constexpr float kWideSpin = 0.75f;              // a spin above it puts a point every 4 Adds, else every 8
constexpr float kAcrossScale = 100000.0f;       // the direction across the track scaled up before it is made a unit
constexpr float kGroundLift = 0.06f;            // the edges' height over the ground
constexpr float kSpinFade = 0.75f;
constexpr float kAlphaScale = 192.0f;
static_assert(std::bit_cast<uint32_t>(kGroundLift) == 0x3d75c28f && std::bit_cast<uint32_t>(kAcrossScale) == 0x47c35000,
              "the original's constants");

}  // namespace

// XBE_GLOBAL(0x00230610, 0x1000)
TrackTexCoord TrackTexCoords[kBatchVertices];
// XBE_GLOBAL(0x00231610, 0x2000)
Coord4 TrackVertices[kBatchVertices];
// XBE_GLOBAL(0x00233610, 0xc)
EAGL::TAR *TrackTextures[kTrackTextures];
// XBE_GLOBAL(0x00233620, 0x800)
uint32_t TrackColours[kBatchVertices];
// XBE_GLOBAL(0x00233e20, 0x180)
UVolatileMaterial TrackMaterials[kTrackMaterials];
// XBE_GLOBAL(0x00233fa0, 4)
uint32_t TrackMaterialIndex;

// FUNC_AT(0x000abfc0)
RTyreTrack* RTyreTrack::Construct(float width, bool longTrack, bool snow) {
    this->width = width;
    count = 0;
    current = 0;
    drawn = 0;
    advanced = 0;
    segments = 0;
    head = 0;
    this->snow = snow;
    capacity = longTrack ? kLongTrack : kShortTrack;
    VU0_v4Init(&points[capacity - 1].left);
    VU0_v4Init(&points[capacity - 1].right);
    return this;
}

// FUNC_AT(0x000ac050)
void RTyreTrack::Init() {
    for (int i = 0; i < kTrackTextures; i++)
        TrackTextures[i] = RTextureContextManager::GetContext(0)->FindOrCreateTexture(kTextureNames[i], 0);
    for (int i = 0; i < kBatchVertices; i++) {
        TrackTexCoords[i].u = (i & 1) ? 0.0f : 1.0f;
        TrackTexCoords[i].v = (i & 2) ? 1.0f : 0.0f;
    }
    for (int i = 0; i < kTrackMaterials; i++) {
        TrackMaterials[i].SetShading(1);
        TrackMaterials[i].SetTextureEnable(true);
        TrackMaterials[i].SetAlphaTestEnable(false);
        TrackMaterials[i].SetTransparencyMethod(1);
    }
}

// The point being moved is kept as a new one, unless the last Add already moved on; the strip starts again at
// the next.
// FUNC_AT(0x000ac170)
void RTyreTrack::Break(int surface) {
    if (segments != 0 && !advanced) {
        if (count < capacity) {
            count++;
            drawn = count;
        } else {
            head = (head + 1) & (capacity - 1);
        }
    }
    advanced = 0;
    segments = 0;
    colours[current] = kColourDefault;
    if (surface == kSurface3) {
        colours[current] = kColourSurface3;
        return;
    }
    if (!snow) {
        if (surface == kSurface9)
            colours[current] = kColourGrey;
        else if (surface == kSurface8)
            colours[current] = kColourWhite;
    }
}

// The ring from its oldest point, in batches of at most kBatchVertices - 2 vertices (a batch that fills draws its
// last point again first in the next). Where the strip starts again, the last vertex and the point's first are
// repeated with no colour.
// FUNC_AT(0x000ac250)
void RTyreTrack::Draw() {
    if (count <= 1)
        return;
    fgRenderer->renderContext->SetZWritesEnable(0);
    int left = drawn;
    int index = head;
    if (left == capacity && !advanced) {
        index = (head + 1) & (capacity - 1);
        left--;
    }
    while (left != 0) {
        Coord4 *vertex = TrackVertices;
        uint32_t *colour = TrackColours;
        int vertices = 0;
        while (left != 0) {
            Point &point = points[index];
            if (!point.joined && colour != TrackColours) {
                VU0_v4copy(&vertex[-1], &vertex[0]);
                VU0_v4copy(&point.left, &vertex[1]);
                vertex[1].w = 1.0f;
                vertex += 2;
                colour[0] = 0;
                colour[1] = 0;
                colour += 2;
                vertices += 2;
            }
            VU0_v4copy(&point.left, &vertex[0]);
            VU0_v4copy(&point.right, &vertex[1]);
            vertex[1].w = 1.0f;
            vertex += 2;
            colour[0] = colours[index];
            colour[1] = colours[index];
            colour += 2;
            vertices += 2;
            if (colour >= &TrackColours[kBatchVertices - 2])
                break;
            left--;
            index = (index + 1) & (capacity - 1);
        }
        if (vertices > 2) {
            UVolatileMaterial *material = &TrackMaterials[TrackMaterialIndex];
            TrackMaterialIndex = (TrackMaterialIndex + 1) & (kTrackMaterials - 1);
            EAGL::TAR *texture;
            if (snow) {
                material->SetAlphaBlendMode(kBlendSubtract);
                texture = width < kNarrowTrack ? TrackTextures[kTextureSnow2] : TrackTextures[kTextureSnow1];
            } else {
                material->SetAlphaBlendMode(kBlendNormal);
                texture = TrackTextures[kTextureSkid];
            }
            VolatileRequests[VolatileRequestIndex]->texture.SetData(texture);
            VolatileRequests[VolatileRequestIndex]->positions.SetData(TrackVertices);
            VolatileRequests[VolatileRequestIndex]->colours.SetData(TrackColours);
            VolatileRequests[VolatileRequestIndex]->texCoords.SetData(TrackTexCoords);
            material->Draw(kTriangleStrip, vertices, NULL);
        }
    }
    fgRenderer->renderContext->SetZWritesEnable(1);
}

// The point's two edges lie across the direction from the previous point, half the width either side, each set on
// the ground (when a face is found under it) and lifted a little.
// FUNC_AT(0x000ac4c0)
void RTyreTrack::Add(const Coord4 *point, float slip, float spin, int surface, WWorldPos *ground) {
    if (snow && count != 0) {
        int last = count < capacity ? count : head;
        if (VU0_v3distancexz(point, &points[Previous(last)].left) < kSnowSpacing)
            return;
    }
    advanced = 0;
    uint8_t spacing = spin > kWideSpin ? 3 : 7;
    int at;
    if (count < capacity) {
        at = count;
        drawn = count + 1;
        if (!(segments & spacing)) {
            count = count + 1;
            advanced = 1;
        }
    } else {
        at = head;
        drawn = capacity;
        if (!(segments & spacing)) {
            head = (head + 1) & (capacity - 1);
            advanced = 1;
        }
    }
    current = at;

    const Coord4 &previous = points[Previous(at)].left;
    alignas(16) Coord4 across;
    across.x = point->z - previous.z;
    across.y = 0.0f;
    across.z = -(point->x - previous.x);
    across.w = 0.0f;        // left unset by the original; nothing reads it
    VU0_v4scale(&across, kAcrossScale, &across);
    VU0_v4unitxyz(&across, &across);
    float halfWidth = width * 0.5f;
    alignas(16) Coord4 right;
    alignas(16) Coord4 left;
    VU0_v4scaleadd(&across, halfWidth, point, &right);
    VU0_v4scaleadd(&across, -halfWidth, point, &left);
    left.y = point->y;
    right.y = point->y;
    ground->FindClosestFace(reinterpret_cast<const Coord3 *>(&right), true);
    if (ground->valid)
        right.y = (float)ground->HeightAtPoint(reinterpret_cast<const Coord3 *>(&right), true);
    ground->FindClosestFace(reinterpret_cast<const Coord3 *>(&left), true);
    // the left edge's height is lifted unrounded
    double leftHeight = left.y;
    if (ground->valid)
        leftHeight = ground->HeightAtPoint(reinterpret_cast<const Coord3 *>(&left), true);
    right.y = right.y + kGroundLift;
    left.y = (float)(leftHeight + kGroundLift);

    Point &added = points[at];
    added.left.x = left.x;
    added.left.y = left.y;
    added.left.z = left.z;
    added.left.w = 1.0f;
    added.right.x = right.x;
    added.right.y = right.y;
    added.right.z = right.z;
    if (segments != 0) {
        float fade = spin * kSpinFade;
        double strength;
        if (slip == 1.0f)
            strength = 1.0f;
        else if (slip > 1.0f)
            strength = slip;
        else
            strength = double(fade) * 0.5f;
        uint32_t alpha = uint8_t(Ftol(strength * kAlphaScale));
        added.joined = 1;
        if (snow)
            colours[at] = alpha << 24 | kColourGrey;
        else if (surface == kSurface3)
            colours[at] = alpha << 24 | kColourSurface3;
        else if (surface == kSurface9)
            colours[at] = (alpha & 0xfe) << 23 | kColourGrey;      // half the alpha
        else if (surface == kSurface8)
            colours[at] = alpha << 24 | kColourWhite;
        else
            colours[at] = alpha << 24 | kColourDefault;
    } else {
        added.joined = 0;
        if (surface == kSurface3)
            colours[current] = kColourSurface3;
        else if (!snow && surface == kSurface9)
            colours[current] = kColourGrey;
        else
            colours[current] = kColourDefault;
    }
    segments++;
}
