#ifndef EDL_H_
#define EDL_H_

#include <stdint.h>

typedef struct {
    uint32_t identifier;
    uint32_t numBlocks;
} edl_section_t;

#pragma pack(push, 1)
typedef struct {
    char magicBytes[3];
    uint8_t algorithmAndEndianness;
    uint32_t decompressedSize;
    uint32_t compressedSize;
} EDLHeader;
#pragma pack(pop)


typedef struct {
    char* dst;
    char* src;
    uint32_t decompressedSize;
    uint32_t compressedSize;
    uint32_t blockCompressionAlgorithm; // ?
    int ourEndianness; // Endianness of the host machine performing the decompression
    int fileEndianness; // Endianness of the file we've been provided
    int errNum;
} EDLDecompressorState;

uint32_t fix_endianness32(EDLDecompressorState *state, uint32_t dataIn);
void EDL_DecompressSection(char* compressedData, edl_section_t* decompressedData);
uint32_t EDL_GetCompressedSize(char* data);
uint32_t EDL_GetDecompressedSize(char* data);
void EDL_Header_Parse(EDLDecompressorState *state);
bool EDL_DecompressBlock(char* src, char* dest);

// The decompression algorithms
void Inflate_directcopy(EDLDecompressorState *state);


#endif // EDL_H_