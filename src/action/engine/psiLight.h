#ifndef PSILIGHT_H
#define PSILIGHT_H

#include "../actionhelpers.h"

// The Xbox layer's lights. Lights are the game's own (the lighting code keeps them); the Xbox layer only hands
// up to four of the ones affecting an object to the GPU.
undefined4 psiLight_Create(void);
bool psiLight_Delete(void);
void psiLight_SetLights(void **lights, ushort count, float r, float g, float b);

#endif // PSILIGHT_H
