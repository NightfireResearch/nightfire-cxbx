#include "../action/helpers.h"

#include <stdio.h>


#define DAT_002434e0 U32_AT(0x002434e0)

int AttemptBigFileLoad(char* param_1, int param_2) {
    return reinterpret_cast<int (*)(char*, int)>(0x00117530)(param_1, param_2);
}
int FUN_0010d980(char* param_1, int param_2) {
    return reinterpret_cast<int (*)(char*, int)>(0x0010d980)(param_1, param_2);
}
int FUN_0010d9c0(char* param_1, int param_2) {
    return reinterpret_cast<int (*)(char*, int)>(0x0010d9c0)(param_1, param_2);
}
int FUN_001174d0(void* param_1, char* param_2) {
    return reinterpret_cast<int (*)(void*, char*)>(0x001174d0)(param_1, param_2);
}


void UFileLoader__LookupAbsolutePath(char *param_1,char *param_2) {
  char cVar1;
  
  if (*param_2 != '\0') {
    cVar1 = *param_2;
    do {
      if (cVar1 == '/') {
        *param_1 = '\\';
      }
      else {
        *param_1 = *param_2;
      }
      param_2 = param_2 + 1;
      cVar1 = *param_2;
      param_1 = param_1 + 1;
    } while (cVar1 != '\0');
  }
  *param_1 = '\0';
}

int UFileLoader__FileLoad(char *param_1,unsigned int param_2,bool param_3) {

  char *pcVar1;
  char cVar2;
  int iVar3;
  char *pcVar4;
  char local_100 [256];

  printf("-------- Loading file %s, %i, %s\n", param_1, param_2, param_3 ? "true" : "false");
  
  pcVar4 = local_100;
  
  UFileLoader__LookupAbsolutePath(local_100, param_1);

  iVar3 = AttemptBigFileLoad(local_100,param_2);
  if (iVar3 == 0) {
    if (param_3) {
                    /* inlined FileLoadDirectFromDisk branch A */
      iVar3 = FUN_0010d980(local_100, param_2);
    }
    else {
                    /* inlined FileLoadDirectFromDisk branch B */
      iVar3 = FUN_0010d9c0(local_100,param_2);
    }
    if (iVar3 == 0) {
      return 0;
    }
  }
  if ((iVar3 != 0) && (DAT_002434e0 == '\x01')) {
                    /* inlined AddFileToRequestList */
    FUN_001174d0((void*)0x243508,local_100);
  }
  return iVar3;
}
