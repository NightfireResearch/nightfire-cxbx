#include "helpers.h"

#define DAT_002adf28 U32_AT(0x002adf28)
#define bShadowCharacter U8_AT(0x001d7825)

// AUTOGEN
unsigned char psiDrawObjectMatrix(int param_1,_MATRIX *param_2);

// FIXME: Call to FUN_000e04e0 is not implemented
// FIXME: Should use AUTOINJ... instead of FUNC_A... but Ghidra database out of date
// FUNC_AT(0x000e0960)
void psiDrawSkinObjectMatrix(int param_1,undefined4 param_2,int param_3,undefined4 param_4) {
  int iVar1;
  
  DAT_002adf28 = param_4;

  static int counter = 0;

  if (counter & 1)
    psiDrawObjectMatrix(param_1,(_MATRIX*)0x002ade8c);

  counter++;
  
  
  if (bShadowCharacter != '\0') {
    iVar1 = *(int *)(*(int *)(param_3 + 0xb8) + 0x98);
    (void) iVar1;
    // FUN_000e04e0(*(undefined4 *)(iVar1 + 0x30),*(undefined4 *)(iVar1 + 0xd18),
    //              *(undefined4 *)(iVar1 + 0x38),param_3,param_1);
  }
  
  return;
}
