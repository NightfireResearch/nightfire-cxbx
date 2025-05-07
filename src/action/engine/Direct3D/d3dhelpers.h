#ifndef _D3DHELPERS_H_
#define _D3DHELPERS_H_

#include "../../actionhelpers.h"

typedef struct _D3DMATRIX {
    union {
        struct {
            float        _11, _12, _13, _14;
            float        _21, _22, _23, _24;
            float        _31, _32, _33, _34;
            float        _41, _42, _43, _44;

        };
        float m[4][4];
        float f[16];
    };
} D3DMATRIX;


void _MATRIXtoD3DMATRIX(_MATRIX* m_in, D3DMATRIX* m_out);


#endif // _D3DHELPERS_H_