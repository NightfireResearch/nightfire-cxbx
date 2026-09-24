#include "psiGraphics.h"

#include "Direct3D/d3dhelpers.h"
#include "Direct3D/d3dSeam.h" // d3dSetMatrix is reimplemented there now, not called through an AUTOGEN passthrough

// AUTOGEN
void RecurseAndDrawBoxes(int geom_idx);
// AUTOGEN
void psiCreateMapTextures(map_tag *mapptr);
// AUTOGEN
void psiCreateEntityGfx(celglist_tag *param_1,map_tag *param_2,uint param_3);


// AUTOINJECT
void psiDrawObjectMatrix(celglist_tag* celglist, _MATRIX* matrix) {

    if (celglist == NULL || celglist->geom_idx == 0) {
        return;
    }
    D3DMATRIX d3dMatrix;
    _MATRIXtoD3DMATRIX(matrix, &d3dMatrix);
    d3dSetMatrix(&d3dMatrix);
    RecurseAndDrawBoxes(celglist->geom_idx);
}