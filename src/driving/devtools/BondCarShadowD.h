#ifndef DRIVING_DEVTOOLS_BONDCARSHADOWD_H_
#define DRIVING_DEVTOOLS_BONDCARSHADOWD_H_

// NIGHTFIRE_BONDCARSHADOWD=1: game/BondCarSimulate.cpp's PBondCar::Simulate and ApplyDamage against the originals,
// on copies of the live cars (the player's and the AI's), perturbed, with the calls beyond the car recorded by
// fakes. Call once the cars exist (the first simulation tick); returns at once unless the variable is set. =2 names
// each case before it runs.
void BondCarShadowD_Run(void);

#endif // DRIVING_DEVTOOLS_BONDCARSHADOWD_H_
