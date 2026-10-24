#ifndef DRIVING_DEVTOOLS_PARTICLESHADOW_H_
#define DRIVING_DEVTOOLS_PARTICLESHADOW_H_

// NIGHTFIRE_PARTICLESHADOW=1: render/Particles.cpp, ParticleCache.cpp and Particulate.cpp against the originals on
// copies of the live particle systems, cache, library and particulate. See ParticleShadow.cpp. Run from the first
// simulation tick, once fgParticleSystems has its library.
void ParticleShadow_Run(void);

#endif // DRIVING_DEVTOOLS_PARTICLESHADOW_H_
