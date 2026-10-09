#ifndef DRIVING_RENDER_PARTICLECACHE_H_
#define DRIVING_RENDER_PARTICLECACHE_H_

// ---------------------------------------------------------------------------------------------------------------
// The particles themselves: RParticle (Ghidra: RParticleSystem::RParticle) and the cache that holds every live one
// (Ghidra: RParticleParticleCache, inside the particle system manager). The systems that spawn them, the library of
// system types and the manager are Particles.h's. See ParticleCache.cpp.
//
// The cache double-buffers: particles[buffer] holds the `count` particles being drawn, and each update moves the
// survivors, and spawns made since, into particles[1 - buffer] (`spawned` of them), then flips. Each buffer is
// walked in blocks of 512, the size of one draw.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"       // Coord3, Coord4

struct RParticleSystemData;
struct RParticleEmitter;

// RParticleSystemData::flags (Particles.h). A particle's flags are their low byte, with kParticleUsesAlphaByte
// worked out for each particle.
enum ParticleFlags : uint32_t {
    kParticleFlag02 = 0x002,            // the system stops after its on and off times (Particles.h)
    kParticleRotates = 0x008,           // drawn turned by its spin
    kParticleSpinFixed = 0x010,         // the spin is an angle, set from the velocity on screen when it spawns
    kParticleStreak = 0x020,            // three vertices from its position back along its velocity
    kParticleUsesAlphaByte = 0x040,     // alpha from the high nibble of alphaSize
    kParticleUsesSizeByte = 0x080,      // size from the low nibble of alphaSize
    kParticleFlag100 = 0x100,           // kParticleUsesAlphaByte for particles whose alphaSize is not 0
    kParticleAdditive = 0x200,          // drawn in the additive list
};

// A particle (0x20 bytes). Its position and velocity are in world units a tick.
struct RParticle {
    Coord3 position;                    // +0x00
    uint8_t life;                       // +0x0c ticks left
    uint8_t flags;                      // +0x0d ParticleFlags
    int8_t gravity;                     // +0x0e added to velocity.y each tick, in -1/4096ths
    uint8_t system;                     // +0x0f its system type's index in the library
    Coord3 velocity;                    // +0x10
    uint8_t alphaSize;                  // +0x1c alpha (high nibble) and size (low nibble), or the system's byte
    uint8_t unknown1D;                  // +0x1d the system's unknown20 times 255
    uint8_t body;                       // +0x1e a rigid body or sticky effect it moves with; 0xff none
    int8_t spin;                        // +0x1f

    // Starts the particle at `position`, from its system type and emitter: the emitter's direction times the
    // type's speed plus random amounts of the emitter's side and of `axis`, plus velocityPerSecond / 60 if
    // given; then moves it back along `motion` and its own velocity by `fraction` of `ticks`, the part of the
    // update since it was due (0x000aadc0, Ghidra: RParticleSystem::RParticle::ResetParticle).
    void ResetParticle(const Coord3 *position, const RParticleSystemData *data, const RParticleEmitter *emitter,
                       const Coord4 *axis, const Coord3 *velocityPerSecond, const Coord4 *motion, float fraction,
                       float ticks);
};
static_assert(sizeof(RParticle) == 0x20, "a particle is 32 bytes");
static_assert(offsetof(RParticle, life) == 0x0c && offsetof(RParticle, velocity) == 0x10 &&
              offsetof(RParticle, alphaSize) == 0x1c && offsetof(RParticle, spin) == 0x1f, "RParticle layout");

enum : int {
    kParticleBlock = 512,                       // particles a block, and a draw
    kParticleBufferSize = 2 * kParticleBlock,   // particles a buffer
};

class RParticleParticleCache {
public:
    int32_t unknown00;                  // +0x00 -1 at construction
    int32_t count;                      // +0x04 particles in particles[buffer]
    int32_t block;                      // +0x08 the block being updated or drawn; -1 between
    uint32_t lastTick;                  // +0x0c the game tick of the last UpdateAndRender
    RParticle particles[2][kParticleBufferSize];   // +0x10
    int32_t buffer;                     // +0x10010 0 or 1: the one being drawn
    int32_t spawned;                    // +0x10014 particles in particles[1 - buffer]

    RParticleParticleCache* Construct();                                                        // 0x000aaa60
    // A new particle's slot in the next buffer, or NULL when the two buffers' counts reach a buffer's size.
    RParticle* Spawn();                                                                         // 0x000aabd0
    // Moves block `block`'s particles a tick on; the living ones go to the next buffer.
    void UpdateBlock(int block);                                                                // 0x000aac20
    // Every block a tick on, then the buffers flipped.
    void JustUpdate();                                                                          // 0x000aad60
    // Up to three ticks' updates, by the game ticks since the last call, then every block drawn.
    void UpdateAndRender();                                                                     // 0x000abf20
    // Block `block`'s particles into the three vertex lists, which are then drawn.
    void RenderBlock(int block);                                                                // 0x000aba10
    // One particle's camera-facing quad into the normal or the additive list, *normalCount or *additiveCount
    // advanced by its four vertices; `age` is its life gone, 0 to 1 (Ghidra: FUN_000ab650; name ours). `block`
    // is not used.
    void RenderParticle(int block, int *normalCount, int *additiveCount, const RParticleSystemData *data,
                        RParticle *particle, Coord4 colour, float age);                         // 0x000ab650
};
static_assert(sizeof(RParticleParticleCache) == 0x10018, "RParticleParticleCache is 0x10018 bytes");
static_assert(offsetof(RParticleParticleCache, particles) == 0x10 &&
              offsetof(RParticleParticleCache, buffer) == 0x10010 &&
              offsetof(RParticleParticleCache, spawned) == 0x10014, "RParticleParticleCache layout");

#endif // DRIVING_RENDER_PARTICLECACHE_H_
