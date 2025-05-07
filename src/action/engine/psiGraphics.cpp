#include "psiGraphics.h"

#include "Direct3D/d3dhelpers.h"

// AUTOGEN
void d3dSetMatrix(D3DMATRIX* matrix);
// AUTOGEN
void RecurseAndDrawBoxes(int geom_idx);

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