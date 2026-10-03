#ifndef DRIVING_GFX_D3DSEAM_H_
#define DRIVING_GFX_D3DSEAM_H_

// The driving engine's graphics seam: every D3D8 and XGRAPHC entry point in the XBE is patched to reach the
// shared D3D9 backend (src/common/gfx/) instead of Microsoft's statically linked library. See d3dSeam.cpp for
// why the seam is at that boundary rather than inside EAGL, and docs/driving-engine-plan.md section 6.1.
void Inject_D3dSeam(void);

// Which patched entry points have been reached without an implementation, and how often. Printed with the
// backend's periodic frame-timing report; this is the list that says what to implement next.
void D3dSeam_ReportMissing(void);

#endif // DRIVING_GFX_D3DSEAM_H_
