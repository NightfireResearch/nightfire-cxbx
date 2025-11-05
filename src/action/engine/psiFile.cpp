#include <stdio.h>
#include <windows.h>
#include "../actionhelpers.h"


int allocateAndLoadFileWithinArchive(char* a, unsigned short b, int* c) {
	int (*funcPtr)(char*, unsigned short, int*) = (int (*)(char*, unsigned short, int*))(0x000dc990);
	return funcPtr(a, b, c);
}

// AUTOGEN
int FUN_000eed6b(int param_1);

// AUTOGEN
void psiFileLoadForParse(char *param_1);

// AUTOINJECT
int psiFileOpen(char* param_1)
{
  printf("hooked psiFileOpen: %s\n", param_1);

  U32_AT(0x002adf74) = 0;
  U32_AT(0x002adf78) = 0;
  U32_AT(0x002adf7c) = 0;
  U32_AT(0x002adf74) = allocateAndLoadFileWithinArchive(param_1,0x1204,(int*)0x002adf78);
  U32_AT(0x002adf7c) = 0;
  return (((uint32_t)U32_AT(0x002adf74) >> 8) << 8 | 1);
}

void dumpToFile(char* gamefile, void* data, size_t len) {
    // Open the file in binary write mode

	char filename[256];

	snprintf(filename, sizeof(filename), "dump/%s", gamefile);

    FILE* file = fopen(filename, "wb");

    if (file == NULL) {
        // Handle error if the file couldn't be opened
        perror("Error opening file");
        return;
    }

    // Write the data to the file
    size_t written = fwrite(data, 1, len, file);

    if (written != len) {
        // Handle error if not all data could be written
        perror("Error writing to file");
    } else {
        printf("Data written to %s successfully\n", filename);
    }

    // Close the file
    fclose(file);
}

#define SingleFileMode U8_AT(0x002adf70)
#define DirFileLen U32_AT(0x00279174)
#define dirFileBuf U32_AT(0x00279168)




int ** __cdecl psiFileLoadOrig(char *filename, unsigned short allocType, int *sizeOut)
{
  int **ppiVar1;

  //printf("psiFileLoad: %s - 0x%04x\n", filename, allocType);

  if (SingleFileMode == '\0') {
    ppiVar1 = (int **)allocateAndLoadFileWithinArchive(filename,allocType,sizeOut);
    printf("psiFileLoad in multi-file mode: %s is 0x%08x bytes starting at 0x%08x, type %04x\n", filename, *sizeOut, ppiVar1, allocType);
    dumpToFile(filename, (void*)ppiVar1, *sizeOut);
    return ppiVar1;
  }
  if (sizeOut != NULL) {
    *sizeOut = DirFileLen;
  }
  printf("psiFileLoad in single-file mode: %s is 0x%08x bytes at the location pointed to by dirFileBuf(0x00279168), type %04x\n", filename, *sizeOut, allocType);
  dumpToFile(filename, *(void**)0x00279168, *sizeOut);
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
