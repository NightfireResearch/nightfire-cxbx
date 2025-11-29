#include "../actionhelpers.h"

#include "Loader.h"
#include "parsemap.h"
#include "celglist.h"
#include "../util/hashtable.h"
#include "../ui/MenuManager.h"
#include "Script.h"

#include <stdio.h>

#define MemType U32_AT(0x00274c98)
#define DirFileType U32_AT(0x00279184)
#define DirFileHash ((HASHCODE)U32_AT(0x002791c8))
#define dirFileBuf (*(uint**)(0x00279168))
#define LoadableIndex U8_AT(0x00279180)


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

    MemType = 0;

    switch(DirFileType) {
        case 1:
          AnimPostLoadInit();
          if (parsemap_parsemap(DirFileHash,'\x01')) {
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
          if (parsemap_parsemap(DirFileHash,'\0')) { // Returns fasle on completion, true if there's more to parse
            return false;
          }
          break;
        case 0xc:
          if (LoadableIndex < ARRAY_SIZE(LoadableFiles)) {
            LoadableFiles[LoadableIndex].hashcode = (HASHCODE) *dirFileBuf;
            LoadableFiles[LoadableIndex].loadableIdx = (char)dirFileBuf[1];
            LoadableIndex++;
          }
        }
        return true;

}

// AUTOINJECT
bool isLoadable(HASHCODE param_1) {
  for (int i = 0; i < LoadableIndex; i++) {
    if (LoadableFiles[i].hashcode == param_1)
      return LoadableFiles[i].loadableIdx != 0;
  }
  return false;
}