#include "../actionhelpers.h"

#include "Loader.h"

#define MemType U32_AT(0x00274c98)
#define DirFileType U32_AT(0x00279184)
#define DirFileHash ((HASHCODE)U32_AT(0x002791c8))
#define dirFileBuf (*(uint**)(0x00279168))
#define LoadableIndex U8_AT(0x00279180)
#define CONST_ZERO_VECTOR (*(_VECTOR *)0x0029d728)

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
  uint maybeHashcode;
  char someChar;
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
            LoadableFiles[LoadableIndex].maybeHashcode = *dirFileBuf;
            LoadableFiles[LoadableIndex].someChar = (char)uVar1;
            LoadableIndex++;
          }
        }
        return true;

}