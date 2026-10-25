#ifndef DRIVING_DEVTOOLS_AUDIOMIXSHADOW_H_
#define DRIVING_DEVTOOLS_AUDIOMIXSHADOW_H_

// A shadow test of the audio mixes, effects, listener and faders (audio/Mix.cpp, audio/Fader.cpp): originals
// against ports, from the first simulation tick, when NIGHTFIRE_AUDIOMIXSHADOW is set non-zero. See
// AudioMixShadow.cpp.
void AudioMixShadow_Run(void);

#endif // DRIVING_DEVTOOLS_AUDIOMIXSHADOW_H_
