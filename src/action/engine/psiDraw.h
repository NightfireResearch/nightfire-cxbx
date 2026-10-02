#ifndef PSIDRAW_H
#define PSIDRAW_H

#include "../actionhelpers.h"
#include "celglist.h"

// Eurocom's Xbox drawing layer (0x000dcac0-0x000e09c0): what the game's render code calls to draw models,
// particles, sprites, shards and character shadows, and to set the camera, fog and blend state. It drives the
// d3d* functions of Direct3D/d3dSeam.cpp.

void RecurseAndDrawBoxes(int gfxEntityIdx);
void psiDrawParticleList(const float *positions, int count, uint32_t colour, int texture, const uint32_t *colours,
                         char blend, float size);
void ConfigureGammaForLevel(void);
void psiPostDraw(void);
void psiSetUpColourBlend(int opaque);
void psiUseCamera(void *viewer);
void psiSetScreenBlur(uint8_t amount);
void psiFog(char enable, float nearDist, float farDist, uint8_t r, uint8_t g, uint8_t b);
void psiSetTweakARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b);
void psiSetTweakA(uint8_t a);
void psiFadeView(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void psiInput_PollDevices_Thunk(void);
int psiStubMinusOne(void);          // 0x000de5d0, a stub the effects code calls
uint8_t psiStubFalse(void);       // 0x000de5e0, a stub the drones and particles call

void *psiGetTriList(void *obj);
void psiDeleteShard(void **shard);
void *psiCreateShard(const float *vertices);
void psiDrawShard(void *vtxData, _MATRIX *matrix, int texture);
void psiSetShardRenderStates(void);
void psiClearZ(void);
uint32_t psiCopyToSP(uint32_t value);

void maybe_psiDrawShadow(float x, float y, float z, void *obj, celglist_tag *glist);
void psiDrawSkinObjectMatrix(celglist_tag *glist, _MATRIX *unused, void *obj, void *palette);

#endif // PSIDRAW_H
