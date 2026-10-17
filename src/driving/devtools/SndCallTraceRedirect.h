#ifndef DRIVING_DEVTOOLS_SNDCALLTRACEREDIRECT_H_
#define DRIVING_DEVTOOLS_SNDCALLTRACEREDIRECT_H_

// driving_standalone_test compiles every source outside the sound library (sound/snd) and SndCallTrace.cpp with this
// header first (CMakeLists.txt), so that each call to one of the SND entries NIGHTFIRE_SNDTRACE logs, and each
// address of one taken, names its wrapper in SndCallTrace.cpp instead: the declarations in the library's headers
// become the wrappers' too. The library's calls to its own API still go straight to the entries. Nothing is
// patched at run time, so the same build runs where code pages cannot be written (signed arm64 code). With
// NIGHTFIRE_SNDTRACE off, each wrapper only calls its entry.

#define SNDSYS_vectortoreal Traced_SNDSYS_vectortoreal
#define SNDSYSI_init Traced_SNDSYSI_init
#define SNDSYS_getopts Traced_SNDSYS_getopts
#define SNDSYS_setops Traced_SNDSYS_setops
#define SNDSYS_restore Traced_SNDSYS_restore
#define SNDSYSI_exithook Traced_SNDSYSI_exithook
#define SNDREAL_systemtask Traced_SNDREAL_systemtask
#define SNDSYS_entercritical Traced_SNDSYS_entercritical
#define SNDSYS_leavecritical Traced_SNDSYS_leavecritical
#define SNDbankadd Traced_SNDbankadd
#define SNDbankheadersize Traced_SNDbankheadersize
#define SNDbankheadercopy Traced_SNDbankheadercopy
#define SNDbankremove Traced_SNDbankremove
#define SNDbankpatpresent Traced_SNDbankpatpresent
#define SNDBANK_play Traced_SNDBANK_play
#define SNDplaysetdef Traced_SNDplaysetdef
#define SNDstop Traced_SNDstop
#define SNDvol Traced_SNDvol
#define SND3dpos Traced_SND3dpos
#define SNDpitchmult Traced_SNDpitchmult
#define SNDfxlevel Traced_SNDfxlevel
#define SNDover Traced_SNDover
#define SNDfxinitbus Traced_SNDfxinitbus
#define SNDfxmasterlevel Traced_SNDfxmasterlevel
#define SNDSTRM_overhead Traced_SNDSTRM_overhead
#define SNDSTRM_create Traced_SNDSTRM_create
#define SNDSTRM_destroy Traced_SNDSTRM_destroy
#define SNDSTRM_queuefile Traced_SNDSTRM_queuefile
#define SNDSTRM_purge Traced_SNDSTRM_purge
#define SNDSTRM_modifyhold Traced_SNDSTRM_modifyhold
#define SNDSTRM_status Traced_SNDSTRM_status
#define SNDSTRM_requeststatus Traced_SNDSTRM_requeststatus
#define SNDSTRM_vol Traced_SNDSTRM_vol
#define SNDSTRM_pitchmult Traced_SNDSTRM_pitchmult
#define SNDSTRM_lowpass Traced_SNDSTRM_lowpass
#define SNDSTRM_3dpos Traced_SNDSTRM_3dpos
#define SNDSTRM_autovol Traced_SNDSTRM_autovol

#endif // DRIVING_DEVTOOLS_SNDCALLTRACEREDIRECT_H_
