#ifndef DRIVING_EAGL_ANIM_ANIMDECODE_H_
#define DRIVING_EAGL_ANIM_ANIMDECODE_H_

// EAGLAnim's shared helpers: delta-compressed value decoding, scratch buffers and attribute blocks. See
// AnimDecode.cpp.

#include <stddef.h>
#include <stdint.h>

struct DofInfo {                     // per value: 12 bytes
    float min;
    float range;                     // a step of the delta
    float start;                     // the value at key 0
};
static_assert(sizeof(DofInfo) == 12, "a DofInfo is 12 bytes");

// The dofs, then the deltas frame by frame.
struct DeltaCompressedData {
    uint16_t count;                  // +0x00 values
    uint8_t bits;                    // +0x02 per delta: 4, 8 or 16
    uint8_t unknown03;
    DofInfo dofs[1];                 // +0x04 [count]

    uint8_t* Stream() { return (uint8_t *)(dofs + count); }
    void DecompressValues(int first, int count, int previousKey, int key, float *source, float *out);  // 0x001069d0
    void DecompressValuesIndexed(int first, int count, int previousKey, int key, float *source, float *out,
                                 int groupSize, uint16_t *index, float scale);                            // 0x00106df0
    double DecompressValue(int value, int previousKey, int key, float previous);                          // 0x00107370
};

struct ScratchBuffer {               // three at 0x00241ba0
    void *buffer;                    // +0x00
    uint32_t size;                   // +0x04
    int32_t users;                   // +0x08

    void AllocateBuffer(uint32_t bytes);                         // 0x00106710
    void FreeBuffer();                                           // 0x00106750
};
static_assert(sizeof(ScratchBuffer) == 0xc, "a ScratchBuffer is 0xc bytes");
ScratchBuffer* ScratchBuffer_GetScratchBuffer(int index);        // 0x00106780
void ScratchBuffer_FreeScratchBuffers();                         // 0x00106790

struct AttributeEntry {              // 8 bytes, sorted by id
    uint16_t id;                     // +0x00
    uint16_t size;                   // +0x02 bytes
    union {                          // +0x04
        uint32_t value;              // up to four bytes: the value itself
        const void *pointer;         // more: where it is
    };
};
static_assert(sizeof(AttributeEntry) == 8, "an attribute entry is 8 bytes");

struct AttributeBlock {
    int32_t count;                   // +0x00
    AttributeEntry entries[1];       // +0x04 [count]

    AttributeEntry* FindAttribute(uint16_t id);                  // 0x001067c0
    bool GetAttribute(uint16_t id, uint32_t *out);               // 0x00106800
    bool CopyAttribute(uint16_t id, void *out);                  // 0x00106830
    bool CopyAttribute2(uint16_t id, void *out);                 // 0x00106880
    bool GetAttributeValue(uint16_t id, void *out);              // 0x001068d0
    bool CopyAttribute3(uint16_t id, void *out);                 // 0x00106930
    bool CopyAttribute4(uint16_t id, void *out);                 // 0x00106980
};

#endif // DRIVING_EAGL_ANIM_ANIMDECODE_H_
