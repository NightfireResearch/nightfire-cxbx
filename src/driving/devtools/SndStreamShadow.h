#ifndef DRIVING_DEVTOOLS_SNDSTREAMSHADOW_H_
#define DRIVING_DEVTOOLS_SNDSTREAMSHADOW_H_

// NIGHTFIRE_SNDSTREAMSHADOW=1 (2: more cases): EA's stream code on the SND side (sound/snd/Streams.cpp: SNDSTRM,
// SNDSTRMI, SNDPKTPLAY) against the originals - every stream header and a sample of the data chunks on the disc,
// the packet player, the API, and whole disc streams played through a synchronous fake STREAM. See
// SndStreamShadow.cpp.
void SndStreamShadow_Run(void);

#endif // DRIVING_DEVTOOLS_SNDSTREAMSHADOW_H_
