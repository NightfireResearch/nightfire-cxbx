#ifndef PSIGRAPHICS_H
#define PSIGRAPHICS_H

#include "../actionhelpers.h"
#include "celglist.h"

void psiDrawObjectMatrix(celglist_tag* celglist, _MATRIX* matrix);
void psiCreateMapTextures(map_tag *mapptr);
void psiCreateEntityGfx(celglist_tag *param_1,map_tag *param_2,uint param_3);

#endif // PSIGRAPHICS_H