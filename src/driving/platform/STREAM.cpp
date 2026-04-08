#include "../"
// AUTOINJECT
undefined1 * STREAM_create(int param_1, int param_2, int param_3, STREAMINTERNAL *param_4, int param_5) {
  
  iVar8 = param_1 * 0x124;
  iVar6 = param_5 - ((param_3 + 0x21) * 0x10 + param_2 * 0xc + iVar8);

  if (
    (((0x17ff < iVar6) && (1 < param_1)) && (param_1 < 0x101)) &&
     (((0 < param_2 && (param_2 < 0x11)) && ((0 < param_3 && (param_3 <= param_2)))))) {

                    /* MRTS = STRM */
    param_4->type = 0x4d525453;
    REALMUTEX_create(&param_4->mutex);
    param_4->field33_0x30 = param_2;
    pSVar1 = param_4 + 1;
    param_4->field35_0x38 = param_3;
    puVar2 = pSVar1->field52_0x7c + iVar8 + -0x7c;
    param_4->field32_0x2c = puVar2;
    param_4->field34_0x34 = puVar2 + param_2 * 0xc;
    piVar4 = (int *)(((uint)(puVar2 + param_2 * 0xc + param_3 * 0x10) & 0xffffff80) + 0x80);
    param_4->maybeOverheadSize = piVar4;
    param_4->field37_0x40 = piVar4;
    param_4->field45_0x60 = piVar4;
    param_4->field46_0x64 = piVar4;
    param_4->field47_0x68 = piVar4;
    param_4->field30_0x24 = (undefined1 *)pSVar1;
    param_4->field31_0x28 = param_1;
    param_4->maybeTotalSize = (int)(param_4->field52_0x7c + param_5 + -0x7c);
    param_4->state = 0;
    param_4->field40_0x4c = 150;
    param_4->field41_0x50 = 50;
    param_4->greedyLevel = 0;
    param_4->field43_0x58 = 0;
    param_4->field44_0x5c = 0;
    param_4->field48_0x6c = 0;
    param_4->field49_0x70 = 0;
    param_4->field50_0x74 = (undefined4 *)0x0;
    param_4->field51_0x78 = (undefined1 *)pSVar1;
    MEM_clear(param_4->field52_0x7c,0x100);
    param_4->field53_0x17c = 0;
    if (iVar6 < 0x4000) {
      param_4->field63_0x18c = 0x800;
    }
    else {
      param_4->field63_0x18c = ((0x7fff < iVar6) - 1 & 0xfffff000) + 0x2000;
    }
    iVar6 = 0;
    if (0 < param_1) {
      iVar7 = 0;
      do {
        piVar4 = (int *)(param_4->field30_0x24 + iVar7);
        *piVar4 = iVar6;
        piVar4[1] = 0;
        iVar3 = iVar7 + 0x124;
        iVar6 = iVar6 + 1;
        iVar7 = iVar7 + 0x124;
        piVar4[3] = (int)(param_4->field30_0x24 + iVar3);
      } while (iVar6 < param_1);
    }
    *(undefined4 *)(param_4->field30_0x24 + iVar8 + -0x118) = 0;
    if (0 < param_2) {
      iVar6 = 0;
      do {
        puVar5 = (undefined4 *)(param_4->field32_0x2c + iVar6);
        iVar6 = iVar6 + 0xc;
        param_2 = param_2 + -1;
        *puVar5 = 0;
        puVar5[1] = 0;
        puVar5[2] = 1;
      } while (param_2 != 0);
    }
    iVar6 = 0;
    if (0 < param_3) {
      iVar8 = 0;
      do {
        puVar5 = (undefined4 *)(param_4->field34_0x34 + iVar8);
        iVar6 = iVar6 + 1;
        iVar8 = iVar8 + 0x10;
        *puVar5 = param_4;
        puVar5[1] = iVar6;
        puVar5[2] = 0;
      } while (iVar6 < param_3);
    }
    return param_4->field34_0x34;
  }
  return NULL;
}