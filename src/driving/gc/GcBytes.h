#ifndef DRIVING_GC_GCBYTES_H_
#define DRIVING_GC_GCBYTES_H_

// The GameCube's driving files are big-endian where its code reads them as words (docs/gc-driving.md): reading them,
// and turning them round in place into the Xbox's little-endian order. No game state: the offline checker uses them.

#include <stdint.h>

static inline uint16_t GcBe16(const uint8_t *p) {
    return uint16_t(p[0] << 8 | p[1]);
}

static inline uint32_t GcBe32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

static inline uint32_t GcLe32(const uint8_t *p) {
    return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0];
}

static inline uint16_t GcLe16(const uint8_t *p) {
    return uint16_t(p[1] << 8 | p[0]);
}

static inline void GcPutLe32(uint8_t *p, uint32_t value) {
    p[0] = uint8_t(value);
    p[1] = uint8_t(value >> 8);
    p[2] = uint8_t(value >> 16);
    p[3] = uint8_t(value >> 24);
}

static inline void GcPutLe16(uint8_t *p, uint16_t value) {
    p[0] = uint8_t(value);
    p[1] = uint8_t(value >> 8);
}

static inline void GcSwap16(uint8_t *p) {
    uint8_t t = p[0];
    p[0] = p[1];
    p[1] = t;
}

static inline void GcSwap32(uint8_t *p) {
    uint8_t t = p[0];
    p[0] = p[3];
    p[3] = t;
    t = p[1];
    p[1] = p[2];
    p[2] = t;
}

static inline float GcBeFloat(const uint8_t *p) {
    union {
        uint32_t word;
        float value;
    } u;
    u.word = GcBe32(p);
    return u.value;
}

#endif // DRIVING_GC_GCBYTES_H_
