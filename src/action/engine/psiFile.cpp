#include <stdio.h>
#include <windows.h>
#include "../actionhelpers.h"
#include "Loader.h"
#include "FS.h"
#include "psiFile.h"
#include "../devtools/FileDump.h"


static int allocateAndLoadFileWithinArchive(char* a, unsigned short b, int* c) {
	return (int)(uintptr_t)FS_AllocateAndLoadBlocking(a, (MallocFlags)b, c);
}

// The open file: psiFileOpen loads the whole of it, and reads walk through it
struct PsiOpenFile {
    uint32_t data;       // where it was loaded, 0 when none is open
    uint32_t size;
    uint32_t position;   // how far reads have got
};
// XBE_GLOBAL(0x002adf74, 0xc)
static PsiOpenFile CurrentFile;

// AUTOGEN
void psiFileLoadForParse(char *param_1);

// AUTOINJECT
int psiFileOpen(char* param_1)
{
  printf("hooked psiFileOpen: %s\n", param_1);

  CurrentFile.data = 0;
  CurrentFile.size = 0;
  CurrentFile.position = 0;
  CurrentFile.data = allocateAndLoadFileWithinArchive(param_1,0x1204,(int*)&CurrentFile.size);
  CurrentFile.position = 0;
  return ((CurrentFile.data >> 8) << 8 | 1);
}

// AUTOINJECT
int psiFileClose(void) {
    CurrentFile.data = 0;
    CurrentFile.size = 0;
    CurrentFile.position = 0;
    return 1;
}

// Reads the file's current position and advances it; there is no allocation or copy, since psiFileOpen has
// already loaded the whole archive. The original passes three more arguments (0x20 or the size, 0x4004 or
// 0x4104, 0x80, and 0 or LoaderLoad's fourth argument - allocation flags, from the look of them), which this
// function never reads.
// AUTOINJECT
int maybePsiFileRead(int length) {
    if (CurrentFile.data == 0)
        return 0;
    uint32_t at = CurrentFile.position;
    CurrentFile.position += length;
    return (int)(CurrentFile.data + at);
}

// XBE_GLOBAL(0x002adf70, 0x1)
static uint8_t SingleFileMode;




int ** __cdecl psiFileLoadOrig(char *filename, unsigned short allocType, int *sizeOut)
{
  int **ppiVar1;

  //printf("psiFileLoad: %s - 0x%04x\n", filename, allocType);

  if (SingleFileMode == '\0') {
    ppiVar1 = (int **)allocateAndLoadFileWithinArchive(filename,allocType,sizeOut);
    printf("psiFileLoad in multi-file mode: %s is 0x%08x bytes starting at 0x%p, type %04x\n", filename, *sizeOut, ppiVar1, allocType);
    FileDump_Save(filename, ppiVar1, *sizeOut);   // settings.ini DumpFiles=on only
    return ppiVar1;
  }
  if (sizeOut != NULL) {
    *sizeOut = DirFileLen;
  }
  printf("psiFileLoad in single-file mode: %s is 0x%08x bytes at the location pointed to by dirFileBuf, type %04x\n", filename, *sizeOut, allocType);
  FileDump_Save(filename, dirFileBuf, *sizeOut);   // settings.ini DumpFiles=on only
  return (int**)dirFileBuf;
}

// FUNC_AT(000dca30)
int ** __cdecl psiFileLoad(char *filename, unsigned short allocType, int *sizeOut)
{
    // Construct the path to the "patch" directory
    char patchPath[256];
    snprintf(patchPath, sizeof(patchPath), "patch/%s", filename);

    // Open the file
    FILE* file = fopen(patchPath, "rb");
    if (file == NULL) {
        // File not found in "patch", call getFile function
        return psiFileLoadOrig(filename, allocType, sizeOut);
    }

    printf("Loading patched file %s in mode %s\n", patchPath, SingleFileMode ? "single" : "multi");

    // Get the length of the file
    fseek(file, 0, SEEK_END);
    int length = ftell(file);
    fseek(file, 0, SEEK_SET);

    // If we're patching, and in single-file mode, we need to write the patched data over the original "dirFileBuf" buffer
    if(SingleFileMode) {
        // Allocate memory to store the file content
        char* fileContent = (char*)malloc(length);
        if (fileContent == NULL) {
            // Handle memory allocation failure
            fclose(file);
            return NULL;
        }

        // Read the file content into memory
        fread(fileContent, 1, length, file);

        // Close the file
        fclose(file);

        // Write the patched data over the original "dirFileBuf" buffer
        // DANGER: What if the patch overflows the original buffer?
        memcpy((void*)dirFileBuf, fileContent, length);

        // We can now free the memory used to store the file content
        free(fileContent);

        // Set the sizeOut parameter if it's not NULL
        if(sizeOut != NULL)
          *sizeOut = length;

        // Return a pointer to the original "dirFileBuf" buffer
        return (int**)dirFileBuf;

    } else {

      // In multi-file mode, we allocate memory for the file content, and return a pointer to it

      // Allocate memory to store the file content
      char* fileContent = (char*)malloc(length);
      if (fileContent == NULL) {
          // Handle memory allocation failure
          fclose(file);
          return NULL;
      }

      // Read the file content into memory
      fread(fileContent, 1, length, file);

      // Close the file
      fclose(file);

      // Return a pointer to the file content, and return the size
      // We never free this mem, YOLO
      if(sizeOut != NULL)
        *sizeOut = length;
      return (int**)fileContent;

    }

}

// Switches between one file per asset and everything in one file; returns the previous mode.
// AUTOINJECT
char psiFileSetSingleFileMode(char mode) {
    char previous = (char)SingleFileMode;
    SingleFileMode = mode;
    return previous;
}
