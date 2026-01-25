#ifndef EDL_H_
#define EDL_H_

#include <stdint.h>

typedef struct {
    uint32_t identifier;
    uint32_t numBlocks;
} edl_section_t;


typedef struct {
    char* dst;
    char* srcData;
    uint32_t decompressedSize;
    uint32_t offsetAt;
    char* compressionAlgo; // ?
    int ourEndianness; // Endianness of the host machine performing the decompression
    int fileEndianness; // Endianness of the file we've been provided
    int errNum;
} maybeEDLDecompressorState;

void maybeEDL_DecompressSection(char* compressedData, edl_section_t* decompressedData);
uint32_t maybeEDL_GetCompressedSize(char* data);
uint32_t maybeEDL_GetDecompressedSize(char* data);

#endif // EDL_H_