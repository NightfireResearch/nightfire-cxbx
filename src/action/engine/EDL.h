#ifndef EDL_H_
#define EDL_H_

#include <stdint.h>

typedef struct {
    uint32_t identifier;
    uint32_t numBlocks;
} edl_section_t;


void maybeEDL_DecompressSection(char* compressedData, edl_section_t* decompressedData);


#endif // EDL_H_