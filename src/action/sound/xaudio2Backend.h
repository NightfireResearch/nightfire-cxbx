#ifndef XAUDIO2BACKEND_H_
#define XAUDIO2BACKEND_H_

#include <stdint.h>
#include "dsndSeam.h"

// ---------------------------------------------------------------------------------------------------------------
// The native XAudio2 backend for the audio seam (dsndSeam.cpp). Selected by "AudioBackend=xaudio2" in
// settings.ini; the default ("cxbx") leaves every DSOUND entry point going to CXBX's HLE exactly as before.
//
// In xaudio2 mode the seam never calls a DSOUND library function: each entry-point macro in dsndSeam.cpp
// dispatches to the XA2_* function attached to it here, or - for anything not implemented yet - to
// DSound_BackendMissing, which counts the call and does nothing. Same arrangement as d3d9Backend.cpp.
//
// Signatures deliberately mirror the DSOUND entry-point typedefs in dsndSeam.cpp exactly, since the dispatch
// wrapper deduces the backend function's signature from the DSOUND one. Every parameter count here has been
// checked against the RET immediate of the real entry point - see the warning in dsndSeam.h.
//
// SCOPE as it stands: the 2D and 3D voice paths. Device and mastering voice, one source voice per sound
// buffer created lazily, Xbox ADPCM decoded and cached, play/stop/loop/position/status, volume and headroom,
// frequency, the 5.1 mixbin volumes collapsed onto a stereo output matrix for 2D voices, and X3DAudio for 3D
// ones with the game's own rolloff curve translated into an X3DAudio distance curve.
//
// Not here yet: the I3DL2 reverb send, which needs the reverb XAPO behind a submix voice, so 3D voices are
// dry. Nor the stream entry points - the XMV decoder creates and drives its own streams by calling DSOUND
// directly, so those calls never reach this seam and CXBX still services them. That is why FMV audio is
// audible in native mode; it also means the stream setters the game does make have to be passed through to
// DSOUND rather than handled here (see DSoundSeamPassThrough in dsndSeam.cpp), and that the streams become
// this backend's problem only once those entry points are hooked at their own addresses.
// ---------------------------------------------------------------------------------------------------------------

// Device and global state
void XA2_DirectSoundCreate(void *lpGuid, DSoundObject **ppDS, void *pUnknown);
void XA2_DirectSoundUseFullHRTF(void);
void XA2_DirectSoundDoWork(void);
void XA2_IDirectSound_DownloadEffectsImage(DSoundObject *thisPtr, const void *pvImageBuffer,
                                           uint32_t dwImageSize, void *pImageLoc, void **ppImageDesc);
void XA2_IDirectSound_CommitDeferredSettings(DSoundObject *thisPtr);

// Listener
void XA2_IDirectSound_SetPosition(DSoundObject *thisPtr, float x, float y, float z, uint32_t dwApply);
void XA2_IDirectSound_SetVelocity(DSoundObject *thisPtr, float x, float y, float z, uint32_t dwApply);
void XA2_IDirectSound_SetOrientation(DSoundObject *thisPtr, float xFront, float yFront, float zFront,
                                     float xTop, float yTop, float zTop, uint32_t dwApply);

// Buffers
void XA2_IDirectSound_CreateSoundBuffer(DSoundObject *thisPtr, DSBUFFERDESC_Xbox *pdsbd,
                                        uint32_t *ppBuffer, uint32_t *ppUnknown);
void XA2_IDirectSoundBuffer_SetBufferData(DSoundBuffer *thisPtr, void *pvBufferData, uint32_t dwBufferBytes);
void XA2_IDirectSoundBuffer_SetFrequency(DSoundBuffer *thisPtr, uint32_t dwFrequency);
void XA2_IDirectSoundBuffer_SetLoopRegion(DSoundBuffer *thisPtr, uint32_t dwLoopStart, uint32_t dwLoopLength);
void XA2_IDirectSoundBuffer_SetCurrentPosition(DSoundBuffer *thisPtr, uint32_t dwPlayCursor);
void XA2_IDirectSoundBuffer_GetCurrentPosition(DSoundBuffer *thisPtr, uint32_t *pdwPlayCursor, uint32_t *pdwWriteCursor);
void XA2_IDirectSoundBuffer_GetStatus(DSoundBuffer *thisPtr, uint32_t *pdwStatus);
void XA2_IDirectSoundBuffer_Play(DSoundBuffer *thisPtr, uint32_t dwReserved1, uint32_t dwReserved2, uint32_t dwFlags);
void XA2_IDirectSoundBuffer_Stop(DSoundBuffer *thisPtr);
void XA2_IDirectSoundBuffer_SetVolume(DSoundBuffer *thisPtr, int32_t lVolume);
void XA2_IDirectSoundBuffer_SetHeadroom(DSoundBuffer *thisPtr, uint32_t dwHeadroom);
void XA2_IDirectSoundBuffer_SetMixBins(DSoundBuffer *thisPtr, DSMIXBINS_Xbox *pMixBins);
void XA2_IDirectSoundBuffer_SetMixBinVolumes(DSoundBuffer *thisPtr, DSMIXBINS_Xbox *pMixBins);

// Not a DSOUND entry point: the seam calls this from dsndWriteVoiceData, after the game has written new ADPCM
// straight into the sample data a buffer is already playing from. That is how in-mission music works - a
// looping voice whose contents SFXUpdateStreams continuously refills ahead of the play cursor - and the
// backend cannot see it any other way, since no DirectSound call is involved. Without it a streamed voice
// plays whatever its buffer happened to contain when it was first bound, which for music is silence.
void XA2_NotifyBufferDataWritten(const void *pvBufferData, uint32_t offset, uint32_t length);

// Per-voice 3D
void XA2_IDirectSoundBuffer_SetMinDistance(DSoundBuffer *thisPtr, float flMinDistance, uint32_t dwApply);
void XA2_IDirectSoundBuffer_SetMaxDistance(DSoundBuffer *thisPtr, float flMaxDistance, uint32_t dwApply);
void XA2_IDirectSoundBuffer_SetPosition(DSoundBuffer *thisPtr, float x, float y, float z, uint32_t dwApply);
void XA2_IDirectSoundBuffer_SetVelocity(DSoundBuffer *thisPtr, float x, float y, float z, uint32_t dwApply);
void XA2_IDirectSoundBuffer_SetRolloffCurve(DSoundBuffer *thisPtr, const float *pflPoints,
                                            uint32_t dwPointCount, uint32_t dwApply);
void XA2_IDirectSoundBuffer_SetI3DL2Source(DSoundBuffer *thisPtr, DSI3DL2BUFFER_Xbox *pds3db, uint32_t dwApply);

#endif // XAUDIO2BACKEND_H_
