#include "EDL.h"


// AUTOGEN
void EDL_Header_Parse(maybeEDLDecompressorState *state);

// AUTOINJECT
uint32_t maybeEDL_GetCompressedSize(char* data) {
    
    maybeEDLDecompressorState decompressor;
    
    decompressor.ourEndianness = 0;
    decompressor.srcData = data;

    EDL_Header_Parse(&decompressor);

    if(decompressor.errNum)
        return 0;

    return decompressor.offsetAt;

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