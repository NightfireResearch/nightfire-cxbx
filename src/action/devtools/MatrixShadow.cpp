// Shadow test for Eurocom's matrix helpers (src/action/engine/Direct3D/xboxMatrix.cpp): each is run, ours and the
// original, on the same random matrices, and the results compared. The originals run on x87 and ours on SSE, so
// a difference in the last bits is allowed. Run at start with MenuShadowTests=on (MenuProbe.cpp).

#include "MatrixShadow.h"

#include "../../common/xbeOriginal.h"
#include "../engine/Direct3D/xboxMatrix.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned s_seed = 12345;
static float Random(float lo, float hi) {
    s_seed = s_seed * 1103515245u + 12345u;
    return lo + (hi - lo) * (float)((s_seed >> 8) & 0xffff) / 65535.0f;
}

static void RandomMatrix(D3DMATRIX *m, bool rigid) {
    for (int i = 0; i < 16; i++)
        m->f[i] = Random(-4.0f, 4.0f);
    if (rigid) {   // a rotation (from a random axis and angle) and a translation
        float ax = Random(-1, 1), ay = Random(-1, 1), az = Random(-1, 1);
        float l = sqrtf(ax * ax + ay * ay + az * az);
        if (l < 1e-3f) { ax = 1; l = 1; }
        ax /= l; ay /= l; az /= l;
        float a = Random(-3.1f, 3.1f), c = cosf(a), s = sinf(a), t = 1 - c;
        m->_11 = t * ax * ax + c;      m->_12 = t * ax * ay - s * az; m->_13 = t * ax * az + s * ay;
        m->_21 = t * ax * ay + s * az; m->_22 = t * ay * ay + c;      m->_23 = t * ay * az - s * ax;
        m->_31 = t * ax * az - s * ay; m->_32 = t * ay * az + s * ax; m->_33 = t * az * az + c;
        m->_41 = m->_42 = m->_43 = 0; m->_44 = 1;
    }
}

static int s_failures;
static void Compare(const char *name, int trial, const D3DMATRIX *ours, const D3DMATRIX *theirs) {
    for (int i = 0; i < 16; i++) {
        float a = ours->f[i], b = theirs->f[i];
        if (fabsf(a - b) > 1e-4f * (1.0f + fabsf(b))) {
            if (s_failures++ < 8)
                printf("[matrix] %s trial %d element %d: ours %g, the original's %g\n", name, trial, i, a, b);
            return;
        }
    }
}

typedef void (__cdecl *MtxFn)(D3DMATRIX *);

static void CheckUnary(const char *name, unsigned address, void (*ours)(D3DMATRIX *), bool rigid) {
    for (int t = 0; t < 64; t++) {
        D3DMATRIX in, a, b;
        RandomMatrix(&in, rigid);
        a = in; b = in;
        ours(&a);
        {
            XbeOriginalScope original(address);
            ((MtxFn)(uintptr_t)address)(&b);
        }
        Compare(name, t, &a, &b);
    }
}

void MatrixShadow_Run(void) {
    s_failures = 0;
    CheckUnary("d3dMatrixIdentity", 0x000e81c0, d3dMatrixIdentity, false);
    CheckUnary("maybeTransposeRotationPart", 0x000e81f0, maybeTransposeRotationPart, false);
    CheckUnary("maybeInvertRigidTransform", 0x000e8920, maybeInvertRigidTransform, true);
    CheckUnary("maybeMtxInverse", 0x000e83a0, maybeMtxInverse, false);

    for (int t = 0; t < 64; t++) {
        D3DMATRIX in, a, b;
        RandomMatrix(&in, false);
        memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
        maybeD3DMATRIXcopy((undefined4 *)&a, (undefined4 *)&in);
        {
            XbeOriginalScope original(0x000e81a0);
            ((void(__cdecl *)(undefined4 *, undefined4 *))(uintptr_t)0x000e81a0)((undefined4 *)&b, (undefined4 *)&in);
        }
        Compare("maybeD3DMATRIXcopy", t, &a, &b);

        float x = Random(-9, 9), y = Random(-9, 9), z = Random(-9, 9);
        a = in; b = in;
        maybeMtxApplyTransform(&a, x, y, z);
        {
            XbeOriginalScope original(0x000e8470);
            ((void(__cdecl *)(D3DMATRIX *, float, float, float))(uintptr_t)0x000e8470)(&b, x, y, z);
        }
        Compare("maybeMtxApplyTransform", t, &a, &b);

        char columns = (char)(t & 1);
        a = in; b = in;
        maybeMatrixAxisScale(&a, x, y, z, columns);
        {
            XbeOriginalScope original(0x000e83b0);
            ((void(__cdecl *)(D3DMATRIX *, float, float, float, char))(uintptr_t)0x000e83b0)(&b, x, y, z, columns);
        }
        Compare("maybeMatrixAxisScale", t, &a, &b);

        float f[9];
        for (int i = 0; i < 9; i++)
            f[i] = Random(-50, 50);
        if (t == 0)
            f[3] = f[0], f[4] = f[1], f[5] = f[2];   // from == to: the zero-length direction
        memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
        MatrixLookAt(&a, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8]);
        {
            XbeOriginalScope original(0x000e8220);
            ((void(__cdecl *)(D3DMATRIX *, float, float, float, float, float, float, float, float, float))(uintptr_t)
                 0x000e8220)(&b, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8]);
        }
        Compare("MatrixLookAt", t, &a, &b);
    }
    printf("[matrix] matrix helpers: %d of 9 x 64 trials differ from the originals\n", s_failures);
}
