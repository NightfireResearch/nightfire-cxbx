#include "../actionhelpers.h"

#include "Loader.h"
#include "celglist.h"
#include "../util/hashtable.h"

#include <stdio.h>

#define MemType U32_AT(0x00274c98)
#define DirFileType U32_AT(0x00279184)
#define DirFileHash ((HASHCODE)U32_AT(0x002791c8))
#define dirFileBuf (*(uint**)(0x00279168))
#define LoadableIndex U8_AT(0x00279180)

// AUTOGEN
void AnimPostLoadInit(void);
// AUTOGEN
void AnimSkeletonProcess(char *);
// AUTOGEN
uint __cdecl parsemap_parsemap(uint hashcode,char param_2);
// AUTOGEN
void AnimLoadFile(HASHCODE hashcode,char param_2);
// AUTOGEN
SCRIPTINFO* Script_Load(HASHCODE param_1,_VECTOR *transform,_VECTOR *rotation,uint *fileBuf,void*, void*, void*);
// AUTOGEN
void __cdecl MenuManager_Load(undefined4 param_1,unsigned int* param_2);

#pragma pack(push, 1)
typedef struct {
  HASHCODE hashcode;
  char loadableIdx;
  char _pad[3];
} LoadableFile;
#pragma pack(pop)

#define LoadableFiles (*(LoadableFile(*)[16])(0x002791d0))

// AUTOINJECT
bool LoaderProcess(void) {

    uint uVar1;
    undefined4 uVar2;
    uint uVar3;

    MemType = 0;

    switch(DirFileType) {
        case 1:
          AnimPostLoadInit();
          uVar2 = parsemap_parsemap(DirFileHash,'\x01');
          if ((char)uVar2 != '\0') {
            return false;
          }
          break;
        case 3:
        case 4:
        case 5:
          AnimLoadFile(DirFileHash,0);
          return true;
        case 6:
          AnimSkeletonProcess((char *)dirFileBuf);
          return true;
        case 7:
          Script_Load(DirFileHash,&CONST_ZERO_VECTOR,&CONST_ZERO_VECTOR,dirFileBuf,NULL, NULL, NULL);
          return true;
        case 8:
          MenuManager_Load(DirFileHash,dirFileBuf);
          return true;
        case 0xb:
          MemType = 1;
        case 0:
        case 2:
          uVar2 = parsemap_parsemap(DirFileHash,'\0');
          if ((char)uVar2 != '\0') {
            return false;
          }
          break;
        case 0xc:
          uVar1 = dirFileBuf[1];
          if (LoadableIndex < ARRAY_SIZE(LoadableFiles)) {
            LoadableFiles[LoadableIndex].hashcode = *dirFileBuf;
            LoadableFiles[LoadableIndex].loadableIdx = (char)uVar1;
            LoadableIndex++;
          }
        }
        return true;

}

typedef struct {
    uint32_t size;
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
void psiCreateMapTextures(map_tag *mapptr);
// AUTOGEN
void __cdecl psiCreateEntityGfx(celglist_tag *param_1,map_tag *param_2,uint param_3);

#define pCurrCelList (*(celglist_tag**)(0x00274c80))
#define FileNextBlock (*(uint**)(0x00274c70))
#define StartingNewMap BOOL8_AT(0x00274c68)
#define m_pmap (*(map_tag**)0x00274b50)

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


// AUTOINJECT
bool isLoadable(HASHCODE param_1) {
  for (int i = 0; i < LoadableIndex; i++) {
    if (LoadableFiles[i].hashcode == param_1)
      return LoadableFiles[i].loadableIdx != 0;
  }
  return false;
}