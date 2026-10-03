#ifndef DRIVING_DEVTOOLS_SNDDECODESHADOW_H_
#define DRIVING_DEVTOOLS_SNDDECODESHADOW_H_

// NIGHTFIRE_SNDDECSHADOW=1: the sound library's decoders (sound/snd/Decode.cpp, DecodeUnused.cpp) against the
// originals - every EA-XA stream packet on the disc, plus random input for EA-XA, PCM16 and MicroTalk. A value
// N > 1 decodes every Nth disc chunk only. See SndDecodeShadow.cpp.
void SndDecodeShadow_Run(void);

#endif // DRIVING_DEVTOOLS_SNDDECODESHADOW_H_
