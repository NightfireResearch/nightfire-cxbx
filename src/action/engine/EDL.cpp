#include "EDL.h"

#pragma pack(push, 1)
typedef struct {
    char magicBytes[3];
    uint8_t algorithmAndEndianness;
    uint32_t decompressedSize;
    uint32_t compressedSize;
} EDLHeader;
#pragma pack(pop)

// AUTOINJECT
uint32_t fix_endianness32(maybeEDLDecompressorState *state, uint32_t dataIn) {
    if(state->ourEndianness == state->fileEndianness)
        return dataIn;
    return ((dataIn & 0xff00) + dataIn * 0x10000) * 0x100 + (dataIn >> 0x10 & 0xff) * 0x100 + (dataIn >> 0x18);
}

// AUTOINJECT
void EDL_Header_Parse(maybeEDLDecompressorState *state) {
   
    EDLHeader* header = (EDLHeader *)state->srcData;

    if ((   (header->magicBytes[0] != 'E') 
        ||  (header->magicBytes[1] != 'D')) 
        ||  (header->magicBytes[2] != 'L')) {
        state->errNum = -3;
        return;
    }
        
    state->fileEndianness = (header->algorithmAndEndianness >> 7);
    uint8_t algorithm = (header->algorithmAndEndianness & 0x7f);
    state->blockCompressionAlgorithm = algorithm;
    
    if (algorithm > 2) {
        state->errNum = -4;
        return;
    }

    state->decompressedSize = fix_endianness32(state, header->decompressedSize);
    state->compressedSize = fix_endianness32(state, header->compressedSize);
    
    state->errNum = 0;
    return;
    
}

// AUTOINJECT
uint32_t maybeEDL_GetCompressedSize(char* data) {
    
    maybeEDLDecompressorState decompressor;
    
    decompressor.ourEndianness = 0;
    decompressor.srcData = data;

    EDL_Header_Parse(&decompressor);

    if(decompressor.errNum)
        return 0;

    return decompressor.compressedSize;

}

// AUTOINJECT
uint32_t maybeEDL_GetDecompressedSize(char* data) {
    maybeEDLDecompressorState decompressor;

    decompressor.ourEndianness = 0;
    decompressor.srcData = data;

    EDL_Header_Parse(&decompressor);

    if(decompressor.errNum)
        return 0;

    return decompressor.decompressedSize;
}


// AUTOGEN
bool maybeEDL_DecompressBlock(char* src, char* dest);

// AUTOINJECT
void maybeEDL_DecompressSection(char* compressedData, edl_section_t *section) {

    if(compressedData == NULL)
        return;
    
    if(section == NULL)
        return;

    if(section->identifier != 0x443584b)
        return;
  
    char* dest = (char*)(section+1); // Data follows immediately after EDL section header
    char* src = compressedData;

    for(int i = section->numBlocks; i != 0; i--) {
        
        uint32_t sizeCompressed = maybeEDL_GetCompressedSize(dest);
        uint32_t sizeDecompressed = maybeEDL_GetDecompressedSize(dest);

        maybeEDL_DecompressBlock(src, dest);
        
        src += sizeCompressed;
        dest += sizeDecompressed;
    
    }


}