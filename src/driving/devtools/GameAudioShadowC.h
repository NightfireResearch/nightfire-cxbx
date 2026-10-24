#ifndef DRIVING_DEVTOOLS_GAMEAUDIOSHADOWC_H_
#define DRIVING_DEVTOOLS_GAMEAUDIOSHADOWC_H_

// NIGHTFIRE_GAMEAUDIOSHADOWC=1: the weapon, world and speech sounds (game/WeaponSound.cpp, CharacterSound.cpp,
// FingoDeathSound.cpp) against the originals, on private objects and on copies of the live ones in the sound list,
// every call out of them recorded by fakes, once, from the first simulation tick. See GameAudioShadowC.cpp.
void GameAudioShadowC_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEAUDIOSHADOWC_H_
