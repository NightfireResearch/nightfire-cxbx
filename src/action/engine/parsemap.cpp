#include "parsemap.h"

#include "psiFile.h"
#include "psiGraphics.h"
#include "../memory.h"

#define pCurrCelList (*(celglist_tag**)(0x00274c80))
#define FileNextBlock (*(uint**)(0x00274c70))
#define StartingNewMap BOOL8_AT(0x00274c68)
#define m_pmap (*(map_tag**)0x00274b50)

// Also used in Loader.cpp
#define MemType U32_AT(0x00274c98)

typedef struct block_header_tag {
    uint size;
    uint identifier;
} block_header_tag;

typedef struct {
    uint32_t size; // maybe block_header_tag?
    HASHCODE hashcode;
    uint32_t applyFlagsToObject;
    float boundSphereX;
    float boundSphereY;
    float boundSphereZ;
    float boundSphereRadius;
    _VECTOR extentMin;
    _VECTOR extentMax;
} block_entity_data;

// AUTOGEN
bool parsemap_parsenextblock(char param_1);

// AUTOGEN
void parsemap_block_map_data_dynamic(block_header_tag *bh, uchar* param_2, uchar doCreation);

#define DynamicBH (*(block_header_tag*)0x00274b34)
#define DynamicPtr PTR_AT(0x00274c58)
#define MapHashCode U32_AT(0x00274c88)
#define ParseMap_State U32_AT(0x00274ca4)
#define filename (*(char*)0x00274b58)
#define FileLastBlock U32_AT(0x00274c74)
#define FileDiscard U32_AT(0x00274c78)

// AUTOINJECT
bool parsemap_parsemap(uint hashcode, bool secondPass) {

    switch(ParseMap_State) {
        case 0: {
            // Load the file into RAM?
            const char* filePath = "";
            sprintf(&filename, "%s%8.8X.bin", filePath, hashcode);
            psiFileLoadForParse(&filename);
            MapHashCode = hashcode;
            DynamicBH.size = 0;
            DynamicPtr = NULL;
            StartingNewMap = 1;
            ParseMap_State = 1;
            return true;
        }
        case 1: {
            // Run parsemap_parsenextblock until completion
            bool complete = false;
            do {
                complete = parsemap_parsenextblock(secondPass);
            } while(!complete);
            
            
            ParseMap_State = 2;
            return true;
        }
        case 2: {
            // If in the second pass, do stuff with dynamic data?
            if(!secondPass) {
                parsemap_block_map_data_dynamic(&DynamicBH, (uchar*)DynamicPtr, false);
            }
            ParseMap_State = 3;
            return true;
        }
        case 3: {
            // If in the second pass, shrink memory?
            if(!secondPass) {
                Mem_Shrink((void**)&FileDiscard, FileLastBlock - FileDiscard);
            }
            ParseMap_State = 0;
            break;
        }

    }

    return false; // Nothing more to do
}


// AUTOINJECT
void parsemap_block_entity_params(void) {

    // Load the given cel from the file

    celglist_tag* currentCelGlist = pCurrCelList;
    
    block_entity_data* data = (block_entity_data*)FileNextBlock;

    currentCelGlist->applyFlagsToObject = data->applyFlagsToObject;
    currentCelGlist->boundSphere.x = data->boundSphereX;
    currentCelGlist->boundSphere.y = data->boundSphereY;
    currentCelGlist->boundSphere.z = data->boundSphereZ;
    currentCelGlist->boundSphere.r = data->boundSphereRadius;
    currentCelGlist->extentMin.x = data->extentMin.x;
    currentCelGlist->extentMin.y = data->extentMin.y;
    currentCelGlist->extentMin.z = data->extentMin.z;
    currentCelGlist->extentMax.x = data->extentMax.x;
    currentCelGlist->extentMax.y = data->extentMax.y;
    currentCelGlist->extentMax.z = data->extentMax.z;

    // If loading a hashcode-referenced piece of geometry, add it to the hashmap
    if(data->hashcode != 0xFFFFFFFF) {
        hashtable_additem(data->hashcode, currentCelGlist);
    }

    // Map textures
    if(StartingNewMap) {
        psiCreateMapTextures(m_pmap);
        StartingNewMap = false;
    }

    psiCreateEntityGfx(pCurrCelList, m_pmap, MemType);

    pCurrCelList++;

}
