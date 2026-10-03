#ifndef DRIVING_EAGL_ANIM_ANIMDECODE_H_
#define DRIVING_EAGL_ANIM_ANIMDECODE_H_

// EAGLAnim's shared helpers: delta-compressed value decoding, scratch buffers and attribute blocks. See
// AnimDecode.cpp.

#include <stdint.h>

struct DofInfo {                     // per value: 12 bytes
    float min;
    float range;
    float start;
};

// u16 value count, u8 bits per delta (4, 8 or 16), u8, DofInfo[count], then the deltas frame by frame.
struct DeltaCompressedData {
    void DecompressValues(int first, int count, int previousKey, int key, float *source, float *out);  // 0x001069d0
    void DecompressValuesIndexed(int first, int count, int previousKey, int key, float *source, float *out,
                                 int groupSize, uint16_t *index, float scale);                            // 0x00106df0
    double DecompressValue(int value, int previousKey, int key, float previous);                          // 0x00107370
};

struct ScratchBuffer {               // three at 0x00241ba0
    void *buffer;
    uint32_t size;
    int32_t users;

    void AllocateBuffer(uint32_t bytes);                         // 0x00106710
    void FreeBuffer();                                           // 0x00106750
};
ScratchBuffer* ScratchBuffer_GetScratchBuffer(int index);        // 0x00106780
void ScratchBuffer_FreeScratchBuffers();                         // 0x00106790

struct AttributeEntry {              // 8 bytes, sorted by id
    uint16_t id;
    uint16_t size;
    uint32_t value;                  // the value, or a pointer to it
};

struct AttributeBlock {              // u32 count, then the entries
    AttributeEntry* FindAttribute(uint16_t id);                  // 0x001067c0
    bool GetAttribute(uint16_t id, uint32_t *out);               // 0x00106800
    bool CopyAttribute(uint16_t id, void *out);                  // 0x00106830
    bool CopyAttribute2(uint16_t id, void *out);                 // 0x00106880
    bool GetAttributeValue(uint16_t id, void *out);              // 0x001068d0
    bool CopyAttribute3(uint16_t id, void *out);                 // 0x00106930
    bool CopyAttribute4(uint16_t id, void *out);                 // 0x00106980
};

#endif // DRIVING_EAGL_ANIM_ANIMDECODE_H_
