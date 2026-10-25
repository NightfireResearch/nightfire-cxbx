#ifndef GC_GCBYTES_H_
#define GC_GCBYTES_H_

// The GameCube's files are big-endian where its code reads them as words (docs/gc-data.md): reading them, and turning
// them round in place into the Xbox's little-endian order. No game state: the offline checker uses them too.

#include <stdint.h>

static inline uint16_t GcBe16(const uint8_t *p) {
    return (uint16_t)(p[0] << 8 | p[1]);
}

static inline uint32_t GcBe32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static inline uint32_t GcLe32(const uint8_t *p) {
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

static inline void GcPutLe32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
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

// Every whole word of bytes bytes from p
static inline void GcSwapWords(uint8_t *p, uint32_t bytes) {
    for (uint32_t i = 0; i + 4 <= bytes; i += 4)
        GcSwap32(p + i);
}

#endif // GC_GCBYTES_H_
