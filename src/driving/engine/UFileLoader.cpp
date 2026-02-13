#include "../platform/FILE.h"

#include "UFileLoader.h"
#include "FileNameList.h"

#include <stdio.h>

// Inlined - can't be injected
bool UFileLoader::LookupAbsolutePath(char *pathOut, char *pathIn) {
  char tmp;
  
  if (*pathIn != '\0') {
    tmp = *pathIn;
    do {
      if (tmp == '/') {
        *pathOut = '\\';
      }
      else {
        *pathOut = *pathIn;
      }
      pathIn = pathIn + 1;
      tmp = *pathIn;
      pathOut = pathOut + 1;
    } while (tmp != '\0');
  }
  *pathOut = '\0';
  return true;
}

// Inlined - can't be injected
void * UFileLoader::FileLoadDirectFromDisk(char* param_1, int param_2, bool z_variant) {
    if (z_variant) {
        return FILE_load(param_1, param_2);
    }
    return FILE_loadz(param_1, param_2);
}

#define DAT_002434e0 U8_AT(0x002434e0)

// Inlined - can't be injected
void UFileLoader::AddFileToRequestList(char* fname) {
    if(DAT_002434e0 == 1) { // Path does not seem to be taken in very limited testing...
        ((FileNameList*)(0x243508))->AddFile(fname);
    }
}

#define someFileNameList ((FileNameList*)(0x00243514))
#define someOtherFileNameList ((FileNameList*)(0x002434e4))

void* UFileLoader::AttemptBigFileLoad(char *param_1,undefined4 param_2) {
  char tmp;
  char *pcVar2;
  FileNameList *fnl;
  char local_100 [256];
  
  local_100[0] = '|';

  pcVar2 = param_1;
  do {
    tmp = *pcVar2;
    pcVar2[(int)(local_100 + (1 - (int)param_1))] = tmp;
    pcVar2 = pcVar2 + 1;
  } while (tmp != '\0');


  void* iVar3 = FILE_loadpackz(local_100, param_2);
  if (DAT_002434e0 != '\0') {
    fnl = someFileNameList;
    if (iVar3 == NULL) {
      fnl = someOtherFileNameList;
    }
    fnl->AddFile(param_1);
  }
  return iVar3;
}


#include <direct.h>
static void ensureDirs(const char *path)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char old = *p;
            *p = '\0';
            //printf("Making %s\n", tmp);
            _mkdir(tmp);              // ignore error (EEXIST is fine)
            *p = old;
        }
    }
}

void dumpToFile(char* gamefile, void* data, size_t len) {

    // Open the file in binary write mode

	char filename[256];

	snprintf(filename, sizeof(filename), "dump_driving/%s", gamefile);

    ensureDirs(filename);

    FILE* file = fopen(filename, "wb");

    if (file == NULL) {
        // Handle error if the file couldn't be opened
        perror("Error opening file\n");
        return;
    }

    // Write the data to the file
    size_t written = fwrite(data, 1, len, file);

    if (written != len) {
        // Handle error if not all data could be written
        perror("Error writing to file");
    } else {
        printf("-- Dumped %s (%i bytes)\n", filename, len);
    }

    // Close the file
    fclose(file);
}
// AUTOGEN
size_t MEM_size(void* data);

// AUTOINJECT
void* UFileLoader::FileLoad(char *rawPath, int param_2, bool param_3) {

    char fixedPath [256];

    // Normalise file path
    UFileLoader::LookupAbsolutePath(fixedPath, rawPath);

    // OUR DEBUG: Print details
    printf("-------- Loading file %s, %i, %s\n", fixedPath, param_2, param_3 ? "true" : "false");

    // First try from BIG archive
    void* loadedFile = UFileLoader::AttemptBigFileLoad(fixedPath, param_2);

    // Otherwise try direct file
    if (loadedFile == NULL) {
        loadedFile = UFileLoader::FileLoadDirectFromDisk(fixedPath, param_2, param_3);
    }

    // No luck either from archive or filesystem means the file is missing
    if(loadedFile == NULL)
        return NULL;

    // OUR DEBUG: Retrieve file size and dump to file
    size_t fileSize = MEM_size(loadedFile);
    dumpToFile(fixedPath, loadedFile, fileSize);

    UFileLoader::AddFileToRequestList(fixedPath);

    return loadedFile;
}

// AUTOINJECT
void* UFileLoader::FileLoadz(char * fname, int flags) {
    return FileLoad(fname, flags, false);
}