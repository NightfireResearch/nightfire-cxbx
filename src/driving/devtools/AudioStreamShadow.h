#ifndef DRIVING_DEVTOOLS_AUDIOSTREAMSHADOW_H_
#define DRIVING_DEVTOOLS_AUDIOSTREAMSHADOW_H_

// NIGHTFIRE_AUDIOSTREAMSHADOW=1, from the first simulation tick: AStream, AStreamPriv, their event queue and the
// stream registry's tree against the originals. See AudioStreamShadow.cpp.
void AudioStreamShadow_Run(void);

#endif // DRIVING_DEVTOOLS_AUDIOSTREAMSHADOW_H_
