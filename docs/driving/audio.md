# The driving engine's audio framework

EA's audio framework between the game and the sound library ([sound.md](sound.md)): the sound manager, the sounds
and their voices, mixes, faders, effects, the listener, banks and their name indexes, and streamed audio. Ported on
8 October 2026 by five packages, then merged (`src/driving/audio/`). Every address is Driving.xbe's.

**Status:** 207 functions ported, including entry points Ghidra had not made functions (Generic_FuncReturnsFalse
0x0001c080, AVoice::View's constructor 0x00123820, the deque destructor thunk 0x00122160, ASound and ALimitedSound's
Play through their vtables). `engine.audio` is 100% ours by bytes; what the coverage tool still counts live are
exception funclets. Checked by five shadow tests against the originals, by a trace of every call into the sound
library compared line for line with the baseline's over lockstep runs of missions 1-8, and by those runs' frames.

## Layout

| File | What |
|---|---|
| `audio/Sound.h/.cpp` | ABaseSound (0xc0) and its kinds - the game's hierarchy is flat: ASound, AOneShotSound (and ALimitedSound from it), AWorldSound (WSound in world/SoundGroup.h derives from it), AMenuSoundPriv, ABasic and AStream each derive from ABaseSound directly; the shared GetName (0x00127bd0) is one folded copy. Its operator new/delete keep the sounds in a list |
| `audio/SoundManager.h/.cpp` | ASoundManager (init, restart, pause/resume, mission over, the per-frame BuildPaths) and ASystem |
| `audio/Mix.h/.cpp`, `Fader.h/.cpp` | AMix (mix groups and transitions), AFX (effect modes on the sound library's buses), AListener, AFader (volume fades between mixes); the URefCounter tree code every audio registry shares (RefCounterTree) |
| `audio/Stream.h/.cpp` | AStream (music and speech streams, their queued events - the game's std::deque - and the hold state machine), AStreamPriv |
| `audio/Voice.h/.cpp`, `Bank.h/.cpp` | AVoice and its three AVoice::Views (a sound on the library's voices), the voice debug map; ABank (sound banks) and the helpers shared by every map with 0x18-byte nodes |
| `audio/Index.h/.cpp`, `ZoomObj.h/.cpp` | AIndex (a bank's name index, from its header file), ASound's and the one-shot sounds' Play, AZoomObj |
| `engine/CoreFoundation.h` | `GameEmptyString`: the game's "" (0x00189fb1), returned where the original returns it - its address is compared |

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_AUDIOMGRSHADOW` | first tick | the manager's operations and the sounds' constructors, fades, Play/Stop paths over fake sounds whose virtual calls are recorded |
| `NIGHTFIRE_AUDIOMIXSHADOW` | first tick | mix chains, transitions and volumes, the mix and fader registries, AFX with the library's bus calls recorded |
| `NIGHTFIRE_AUDIOSTREAMSHADOW` | first tick | AStream's Play, Next, Stop, Event and queries on synthetic streams with every library call recorded; the event queue and the registry |
| `NIGHTFIRE_AUDIOVOICESHADOW` | first tick | AVoice and its views under the real sound lock with the library's voice calls recorded; PlayVoices; the banks' patch queries |
| `NIGHTFIRE_AUDIOINDEXSHADOW` | first tick | every loaded bank's index built by both, and 160 synthetic header files; the maps; the one-shot sounds' Play |

**In game:** `NIGHTFIRE_SNDTRACE=1` (devtools/SndCallTrace.cpp) logs every call into the sound library's public API,
tick by tick. With `NIGHTFIRE_LOCKSTEP=1` and `NIGHTFIRE_SNDLOCKSTEP=1` (devtools/SndLockstep.cpp) the sound side
follows the simulation's ticks too - the driver thread's 10 ms steps, the buffers' play positions, the stream
service, stream file reads and TIMER_gettick - so two runs of one build give the same trace, and a port's trace can
be compared with a baseline's line for line (both builds need the two devtools files). Without the sound lockstep,
two runs of one build diverge within a few ticks: voices end by real playback, and the game branches on it.

## What the port taught

- **The trace found what the shadows did not.** The first comparison showed whole groups of SNDBANK_play calls
  missing on six of eight missions while every frame matched: AIndex::Construct searches for the underscore from a
  name's fifth character, so "WPN_GUN_FIRE" is stored as "SFX_FIRE"; the port searched from the first, stored
  "SFX_GUN_FIRE", and the game's lookups missed. The banks loaded by the first tick have no such names.
- **A copy from label + 1.** ABaseSound's operator new stores the label without its first character.
- **The shadows compare pointers to each side's copies.** A stream's mix pointer differed only by where each side's
  synthetic mix was allocated.
- **A restore range does not undo a direct call.** NIGHTFIRE_RESTORE_RANGES puts back the originals' entries, but our
  ports call each other directly, so restoring one package left its code running from the others: bisect by
  replacing, not by restoring, once a subsystem's calls go direct.

## Odd things in the original, kept

- AIndex::Construct sizes its name block in a first pass and can overflow it when a name's prefix is not three
  characters; a file name with no '.' gives strncpy a negative count; a name shorter than four characters is
  searched into uninitialised stack (the port zeroes the buffer).
- AFader::Remove frees the fader whatever its reference count; AFader::Create and AStream::Create look a name up and
  ignore the answer; AFX::Pause sends to bus 0x40, not AFX's own; AMix::Load copies the name with an unbounded strcpy.
- ClearMission asks for the streams as "music"/"speech" in lower case, unlike everywhere else; AMenuSound::Trigger
  looks its patch up twice; AStream::DecodeError adds 19 to its argument and discards it.
