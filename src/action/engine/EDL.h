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
    uint32_t compressedSize;
    uint32_t blockCompressionAlgorithm; // ?
    int ourEndianness; // Endianness of the host machine performing the decompression
    int fileEndianness; // Endianness of the file we've been provided
    int errNum;
} maybeEDLDecompressorState;

uint32_t fix_endianness32(maybeEDLDecompressorState *state, uint32_t dataIn);
void maybeEDL_DecompressSection(char* compressedData, edl_section_t* decompressedData);
uint32_t maybeEDL_GetCompressedSize(char* data);
uint32_t maybeEDL_GetDecompressedSize(char* data);
void EDL_Header_Parse(maybeEDLDecompressorState *state);

#endif // EDL_H_