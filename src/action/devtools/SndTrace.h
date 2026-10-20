#ifndef ACTION_DEVTOOLS_SNDTRACE_H_
#define ACTION_DEVTOOLS_SNDTRACE_H_

// A trace of every call the game's sound code (SFX*, ES_*) makes into the psiSFX layer, a line each with the
// frame, for comparing two builds' sound calls over a time demo. NIGHTFIRE_SNDTRACE=<file> turns it on.
// Values the host's playback decides (a voice still playing, a play position, the timer) are traced as calls
// only, without their answers, so two runs of one build agree.
void SndTrace(const char *format, ...);

#endif // ACTION_DEVTOOLS_SNDTRACE_H_
