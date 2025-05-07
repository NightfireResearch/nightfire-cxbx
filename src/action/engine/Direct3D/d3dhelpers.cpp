#include "d3dhelpers.h"


// Direct3D takes 4x4 matrix in row-major order, but _MATRIX (internal, cross-platform) is shortened to 15 entries and is column-major
void _MATRIXtoD3DMATRIX(_MATRIX* m_in, D3DMATRIX* m_out) {
    m_out->f[0] = m_in->m[0];
    m_out->f[1] = m_in->m[4];
    m_out->f[2] = m_in->m[8];
    m_out->f[3] = m_in->m[0xc];
    m_out->f[4] = m_in->m[1];
    m_out->f[5] = m_in->m[5];
    m_out->f[6] = m_in->m[9];
    m_out->f[7] = m_in->m[0xd];
    m_out->f[8] = m_in->m[2];
    m_out->f[9] = m_in->m[6];
    m_out->f[10] = m_in->m[10];
    m_out->f[11] = m_in->m[0xe];
    m_out->f[12] = 0.0;
    m_out->f[13] = 0.0;
    m_out->f[14] = 0.0;
    m_out->f[15] = 1.0;
}

