#ifndef DRIVING_DEVTOOLS_SHADOWTICKS_H_
#define DRIVING_DEVTOOLS_SHADOWTICKS_H_

// The shadow tests run from the first simulation tick, and the live vehicle shadows from every tick after it
// (Teleport_Tick calls both). Each test is off unless its variable is set. The standalone test build has none.
void ShadowTicks_FirstTick(void);
void ShadowTicks_Tick(void);

#endif // DRIVING_DEVTOOLS_SHADOWTICKS_H_
