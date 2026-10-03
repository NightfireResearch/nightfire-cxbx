#ifndef DRIVING_DEVTOOLS_SNDMIXSHADOW_H_
#define DRIVING_DEVTOOLS_SNDMIXSHADOW_H_

// NIGHTFIRE_SNDMIXSHADOW=1: the software mixer and reverb (sound/snd/Mixer.cpp, Reverb.cpp) against the originals,
// at injection time. See SndMixShadow.cpp.
void SndMixShadow_Run(void);

#endif // DRIVING_DEVTOOLS_SNDMIXSHADOW_H_
