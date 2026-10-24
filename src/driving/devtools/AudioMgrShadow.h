#ifndef DRIVING_DEVTOOLS_AUDIOMGRSHADOW_H_
#define DRIVING_DEVTOOLS_AUDIOMGRSHADOW_H_

// NIGHTFIRE_AUDIOMGRSHADOW=1: the sound manager and the sounds (audio/SoundManager.cpp, audio/Sound.cpp) against
// the originals on private sounds and lists, the calls out of them recorded by fakes, once, from the first
// simulation tick. See AudioMgrShadow.cpp.
void AudioMgrShadow_Run(void);

#endif // DRIVING_DEVTOOLS_AUDIOMGRSHADOW_H_
