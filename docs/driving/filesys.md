# The driving engine's file system layer (FILESYS, ASYNCFILE, FILE_*, big files)

What `Driving.xbe` does between a game-side request such as `FILE_load("data\\sim\\...")` and the Win32 calls that
now sit under it, written so that the layer can be replaced as one unit. Every address is Driving.xbe's (Ghidra
program `/Xbox_EU/Driving.xbe`). Names are Ghidra's unless marked *(invented)*; names in the form *PS2: X* come
from the PS2 build's symbols (`/PS2_EU_51258/DRIVING.ELF`), whose layer has the same API but a different
implementation (CD-DVD device, no worker thread), so only names and intent come from there.

**Status (2 October 2026): ported**, as one unit, in `src/driving/platform/FileSys.cpp`, following section 8. It
fixes 7.1, 7.2, 7.4, 7.8, 7.9 and 7.10, always seeks for subfiles (7.3), and reports a read error instead of
hanging; the rest is as described here. The STREAM helpers (4.11) are left for the STREAM port.

Conventions in this document:

- "cdecl" means arguments on the stack, caller pops, plain `RET`. Every function here is cdecl unless a register
  is named. *Register arguments are what Ghidra gets wrong*; they were checked in the disassembly.
- Offsets are in hex. `op`, `w`, `q`, `slot`, `big` are the records described in section 2.
- "Status" is the signed byte at `op+0x10`: 0 pending/running, 1 succeeded, -2 (`0xfe`) failed, and -1 is what
  callers see for a cancelled op.

Contents: 1 overview, 2 data layouts and globals, 3 threading model, 4 function reference, 5 what it calls
underneath, 6 who calls it, 7 bugs and quirks a port must decide on, 8 port structure and tests.

---------------------------------------------------------------------------------------------------------------

## 1. Overview

Four strata, top down:

| stratum | functions | runs on |
|---|---|---|
| **FILE_*** (whole-file helpers) | `FILE_load`, `FILE_loadz`, `FILE_loadat`, `FILE_sizez`, `FILE_exists`, `FILE_save`, `FILE_loadpackz`, `FILE_unpacksizez` ... | caller, inside `FILESYS_atomic` |
| **FILESYS sync wrappers** | `FILESYS_opensync`, `..._readsync`, `..._closesync`, `..._addbigsync` ... | caller: submit, then wait |
| **FILESYS ops** | `FILESYS_open/close/read/write/size/exists/addbig/delbig`, `opstatus`, `waitop`, `completeop`, `callbackop`, `cancelop`, `priorityop` | caller (submit) + one worker thread (execute) |
| **file device** *(invented name, PS2: FILEDEV_*)* | slot table of open files at `0x00242a34`, `.viv` directory search | worker thread |

ASYNCFILE (`ASYNCFILE_init/load/release/cancel/restore`) is a separate client of the FILESYS ops: a request
table that chains open, size, read and close callbacks. Only `ASYNCFILE_init` and `ASYNCFILE_restore` are still
reachable (from `Bond_StartUpSystem` / `Bond_CleanUp`); its loader, `ASYNCFILE_load`, was only called by the movie
player, which is ours now.

The generic locked queue (`0x0014a540..0x0014aa0f`) is a singly linked list with a count, a priority key
function and a MUTEX. FILESYS is its only user in this binary apart from `FUN_0014a540`, an identity function
the linker folded and that other code (vtables at `0x001d4840`, `0x001d4870`, and `FUN_000a2fc0`) also uses.

A path either names a loose file (`"driving\\misc.viv"`, made into `"D:\\driving\\misc.viv"`) or a file inside a
big file (`"misc.viv|data\\sim\\x"`, or `"|data\\sim\\x"` for "any open big file"). A plain path opened for
reading that is not found on disk is then looked for in every open big file. The game's only big files are
EA `BIGF` archives, `D:\driving\*.viv`.

Function count: 102 addresses were examined (the coverage tool's `platform.files` list, `FUN_0010b5f0`, the queue
helpers, the three STREAM helpers and the undefined code found among them). Section 4 documents **93** that
belong to the layer (including six pieces of code Ghidra has not made functions), plus the three STREAM helpers;
the rest are out of scope and listed in 4.12.

---------------------------------------------------------------------------------------------------------------

## 2. Data layouts and globals

### 2.1 Queue header `q` (0x38 bytes) *(invented name: LOCKQUEUE)*

| off | type | meaning |
|---|---|---|
| +0x00 | int | count |
| +0x04 | uint | flags. bit 0 "changed": set by every mutation, used by the iterator as its stop flag. bit 1 set (value 2) by `FUN_0014a5f0`, cleared (0) by `FUN_0014a640`; never read |
| +0x08 | node* | head |
| +0x0c | node* | tail |
| +0x10 | uint (*)(node*, int) | key function, `FUN_0014a540` (identity: the node's address) when none was given |
| +0x14 | int | key function's second argument |
| +0x18 | MUTEX (0x20) | +0x18 magic (`"Ftum"` once destroyed), +0x1c the Xbox `RTL_CRITICAL_SECTION` (0x1c bytes) |

A node's first dword is its `next` link; nothing else of the node is the queue's.

The queue does **not** lock in every helper: see 4.1 for which take the MUTEX. The critical section is recursive,
and FILESYS relies on that (it locks a worker's pending queue around calls that lock it again).

### 2.2 Worker record `w` (0xb8 bytes, at `FILESYS_operations` = `*(0x00242870)`, stride 0xb8)

| off | type | meaning |
|---|---|---|
| +0x00 | int | alive: set to 1 by the worker thread when it starts, 0 when it leaves. Zero means "no worker yet" everywhere |
| +0x04 | RealThread (0xc) | +0x04 unused, +0x08 thread HANDLE, +0x0c thread id (`THREAD_createparam` fills it) |
| +0x10 | op* | the op being executed, or 0 |
| +0x14 | q | **pending** queue, key `FUN_0010c030` (priority, then sequence) |
| +0x4c | q | **done** queue, key identity; its MUTEX is a byte copy of the pending queue's (7.4) |
| +0x84 | RealSignal (8) | +0x84 unused, +0x88 auto-reset event: "there is work" |
| +0x8c | MUTEX (0x20) | serialises `FILESYS_atomic` on this worker |
| +0xac | HANDLE | manual-reset event: "an op finished" (also the thread's start handshake) |
| +0xb0 | uint | sequence counter, 24 bits, starts at 1, wraps from 0xffffff to 1 |
| +0xb4 | int | priority ceiling: the worker only starts ops whose priority byte is <= this. 0xff normally, lowered by `FILESYS_atomic` |

Two worker records are allocated (0x170 bytes), but **only worker 0 is ever used**: every submitter passes worker
index 0, and `FILESYS_atomic`'s worker argument comes from `dummyGetNullValue` (`0x000f7330`, `return 0`). The
handle format and `FILESYS_atomic` accept indices 0..31 (`0x1f`), which would run off the two records.

### 2.3 Operation record `op` (0x30 bytes)

| off | type | meaning |
|---|---|---|
| +0x00 | op* | queue link |
| +0x04 | uint | handle = `w.seq << 5 | workerIndex`; 0 once completed and back on the free list |
| +0x08 | int | type (table below) |
| +0x0c | uint | flags: bit 0 `+0x2c` is a FILE_malloc'd name to free in `completeop`; bit 1 cancelled; bit 2 a callback was registered through `callbackop`; bit 3 callback has fired. `FUN_0010cbf0` clears the low four bits |
| +0x10 | s8 | status: 0, 1, -2 (`0xfe`) |
| +0x11 | u8 | priority (smaller runs first) |
| +0x14 | int | error: `GetLastError()` after the operation |
| +0x18 | int | file slot handle (`~slotIndex`, see 2.5): the input of close/read/write, the output of open |
| +0x1c | int | user data, passed to the callback |
| +0x20 | void (*)(uint handle, int status, int userData) | callback, run on the worker thread when the op finishes |
| +0x24 | int | argument 0 |
| +0x28 | int | argument 1 |
| +0x2c | int | argument 2 |

| type | submitter | +0x24 | +0x28 | +0x2c | worker does | `completeop` returns |
|---|---|---|---|---|---|---|
| 0 open | `FILESYS_open` | mode | - | name (owned) | `FUN_0010c070(name, mode, mode & 1)` -> `+0x18` | `+0x18` (slot handle); if cancelled, closes the slot and returns 0 |
| 1 close | `FILESYS_close` | - | - | - | nothing (status 1) | `FUN_0010d760(+0x18)`: **the close happens here, on the caller's thread** |
| 2 read | `FILESYS_read` | offset | count, then bytes read | buffer | `FUN_0010d4a0` | `+0x28` |
| 3 write | `FILESYS_write` (0x0010cf20) | offset | count, then bytes written | buffer | `FUN_0010d680` | `+0x28` |
| 4 size | `FILESYS_size` | size | - | - | none: done synchronously by the submitter | `+0x24` |
| 5 | (no submitter) | | | | status 1, error 0 | status == 1 |
| 6 exists | `FILESYS_exists` | 0/1 | - | name (owned) | open with (mode 1, search big files), close at once | `+0x24` |
| 7 delete | (no submitter on Xbox; *PS2: FILESYS_delete*) | | | path | `FUN_0010d7d0` | status == 1 |
| 8 | (no submitter; *PS2: FILESYS_null*) | | | | status 1, error 0 | 0 |
| 9 addbig | `FILESYS_addbig` | - | - | big record | open the archive, read its directory | appends the record to the big list, returns its id |
| 10 delbig | `FILESYS_delbig` | - | - | big record | unlink from the big list | frees directory, closes slot, frees record, returns 1 |

Modes (`+0x24` of open, the second argument of `FUN_0010d2f0`):

| bit | meaning |
|---|---|
| 1 | read only: `GENERIC_READ`, `FILE_SHARE_READ`, and (in `FUN_0010c070`) fall back to big files. Clear: `GENERIC_READ|GENERIC_WRITE`, share 0 |
| 2 | create: with bit 4 `CREATE_ALWAYS` (2), without `CREATE_NEW` (1) |
| 4 | without bit 2: `TRUNCATE_EXISTING` (5). Neither bit: `OPEN_EXISTING` (3) |

Callers use mode 1 (load) and 6 (`FILE_save`, `FileNameList::Dump`: read/write, `CREATE_ALWAYS`).

**Op handle**: `(seq << 5) | worker`. `seq` is never 0, so a handle is never 0, and 0 is every submitter's "failed"
answer (only `FILESYS_addbig` and `FILESYS_delbig` can actually return 0). The worker index of a handle is
`handle & 0x1f`; everything that takes a handle finds its worker that way.

**The free list** is the queue at `0x00242874`: `FILESYS_initadr` pushes all `numOps` records (default 0x20) on it,
`FUN_0010cbf0` pops the head, `FILESYS_completeop` appends the finished op at the tail. An op stays out of the free
list from submission until `completeop`, so **every op must be completed exactly once** or the records run out;
`FUN_0010cbf0` does not check for an empty list and would write through a null pointer (7.1).

### 2.4 Big-file record `big` (0x118 bytes, FILE_malloc'd "BigFile")

| off | type | meaning |
|---|---|---|
| +0x00 | big* | link in the big list (queue at `0x002428d4`) |
| +0x04 | int | id: -1, -2, -3 ... from `0x002428d0`, decremented under the MUTEX at `0x002428ac` |
| +0x08 | int | 0, unused |
| +0x0c | int | slot handle of the open archive |
| +0x10 | u8* | the archive's directory (header), FILE_malloc'd "BF Header" |
| +0x14 | int | directory size in bytes (`FUN_0010ddf0`) |
| +0x18 | char[0x100] | the archive's path as given (e.g. `"driving\\misc.viv"`) - compared with the part before `'|'` |

The record joins the big list only in `FILESYS_completeop` of a successful addbig, i.e. after the caller collects
the result; until then the archive is open but not searched.

### 2.5 File slot `slot` (16 bytes) *(invented name; PS2: FILEDEV)*

Table base `*(0x00242a34)` (Ghidra: `maybeNumFilesysOps` - misleading), count `*(0x00242a30)` (Ghidra:
`maybeNumPrioFilesysOps`), MUTEX at `0x00242a10` (taken only to claim a slot). The table is the first
`numFiles * 16` bytes of the FILESYS block (2.7). A slot handle is `~index` (so -1, -2, ...); all slot functions
check `handle < 0`, `~handle < count`, table non-null and `slot.handle != 0`, else `SetLastError(6)`
(`ERROR_INVALID_HANDLE`) and return 0.

| off | meaning |
|---|---|
| +0x00 | Win32 HANDLE; 1 while claimed but not yet open; 0 free |
| +0x04 | current position (absolute in the underlying file, so for a subfile it runs base..base+size) |
| +0x08 | size (for a subfile, the entry's size; a write past the end grows it) |
| +0x0c | base offset in the underlying file: 0 for a loose file, the entry's offset for a file inside a big file |

A subfile slot **shares the archive slot's HANDLE** (`slot.handle = archiveSlot.handle`) and is "closed" by
clearing it without `CloseHandle` (`FUN_0010d760` only closes when base is 0). Every open archive holds a slot for
as long as it is registered, and every open subfile one more; there are 16 (`FILESYS_init(0x10, ...)`).

### 2.6 Big-file (`.viv`) directory format

`FUN_0010dd80` classifies the first bytes (big-endian reads throughout):

| kind | magic | directory size (`FUN_0010ddf0`) | entries start | offset width | size width |
|---|---|---|---|---|---|
| 1 | `C0 FB` (u16) | `BE16(+2) + 4` | +6 | 3 | 3 |
| 2 | `"BIGF"` | `BE32(+0xc)` | +0x10 | 4 | 4 |
| 3 | `"BIG?"` (any 4th byte) | `BE32(+0xc)` | +0x10 | `hdr[3] - '0'` (unclamped) | `min(hdr[3] - '0', 4)` |
| 0 | anything else | 0 | | | |

Every entry is `offset` (offset-width bytes, BE), `size` (size-width bytes, BE), then a NUL-terminated name.
For offset widths above 4, `FUN_0010dd30` reads the high part from the first `width-4` bytes and the low dword
from the last four; only the low dword is kept. The directory ends at `hdr + size`, or 8 bytes earlier when
those last eight bytes start with a letter followed by three digits (an EA `"L231"`-style footer).

The game's archives are all kind 2 (`BIGF`): `+4` is the archive size (little-endian, unread), `+8` the entry
count (BE, unread), `+0xc` the directory size. Names are stored with backslashes, e.g. `data\sim\attrib\attrib.dir`.
Names are compared case-insensitively (`toupper`, `0x001327f9`); slashes are not converted here (the callers in
`UFileLoader` already turn `/` into `\`).

### 2.7 Globals

| address | Ghidra name | meaning |
|---|---|---|
| `0x001d1c00` | `sync_priority` | 100 (0x64): the priority every FILE_* helper passes to `FILESYS_atomic` |
| `0x001d1c08` | | 0x20 (unreferenced here) |
| `0x001d1c0c` | `pFILE_malloc` | `void *(*)(const char *name, int size, int flags)`; set to `0x00114630` by `FILESYS_setmemcallbacks` |
| `0x001d1c10` | `pFILE_mfree` | `void (*)(void *)`; `0x00114670` |
| `0x001d1c14` | | read-error handler `char (*)(void)`, set by `FUN_0010d840` (to `0x0005c960`). If `ReadFile` fails and the handler returns 0, the reader sleeps forever |
| `0x001d1c28` | | `const char *` root, `"D:"` (`0x001a1940`) |
| `0x00242838` | `mutex` | ASYNCFILE MUTEX |
| `0x00242858` | | ASYNCFILE generation counter, +0x100 per load, skipping 0 |
| `0x0024285c` / `0x00242860` | | ASYNCFILE free list head / tail (link at record +4) |
| `0x00242864` | `numrequests` | ASYNCFILE request count |
| `0x00242868` | `request` | ASYNCFILE request table (count * 0x30), FILE_malloc'd "ASYNCFILE" |
| `0x00242870` | `FILESYS_operations` | worker records; non-zero = initialised |
| `0x00242874` | `DAT_00242874` | free op queue (q, to 0x002428ab) |
| `0x002428ac` | | MUTEX for the big-id counter |
| `0x002428cc` | | op record array base (`FILESYS_operations + 0x170`); written once, read by nothing |
| `0x002428d0` | | next big id, starts at -1, counts down |
| `0x002428d4` | `DAT_002428d4` | big list (q, to 0x0024290b) |
| `0x0024290c` | | worker exit flag; only ever written 0 (`FILESYS_initadr`), so workers never exit |
| `0x00242910` | | sub-directory string inserted between root and path; never written, so always empty |
| `0x00242a10` | | slot table MUTEX |
| `0x00242a30` | `maybeNumPrioFilesysOps` | slot count |
| `0x00242a34` | `maybeNumFilesysOps` | slot table base |
| `0x002475fc` | | STREAM request generation counter (`FUN_0014ac00`; movie/sound layer) |

The FILESYS block (`FILESYS_init`: FILE_malloc "File Sys", `(numOps*3 + 0x17 + numFiles) * 16` bytes, flags
0x100; with the game's (16, 0x32, 32) that is 0x870 bytes), zero-filled:

```
+0                    numFiles * 16      slot table           (0x00242a34)
+numFiles*16          0x170              two worker records   (FILESYS_operations)
+numFiles*16 + 0x170  numOps * 0x30      op records           (0x002428cc), all pushed on the free list
```

---------------------------------------------------------------------------------------------------------------

## 3. Threading model

**Start.** Nothing starts at `FILESYS_init`. The first submission (`FUN_0010cbf0`) or `FILESYS_atomic` on a worker
whose `alive` is 0 calls `FUN_0010cae0` (worker index in `EBX`), which, holding the free-list lock, initialises the
worker's two queues, SIGNAL, events and MUTEX, sets `seq = 1`, `ceiling = 0xff`, and starts
`THREAD_createparam(&w+4, FUN_0010c110, index, 0, 0, priority 1 = THREAD_PRIORITY_ABOVE_NORMAL)`. It then waits on
**worker 0's** `+0xac` event (`INFINITE`) and resets it: the new thread sets `alive = 1` and that event as its
first act.

**Submit** (`FILESYS_open` and siblings): `FUN_0010cbf0(type, prio, userData, worker)` pops a record, fills
type, priority, user data, clears the rest, and - under the pending queue's lock - assigns the handle and bumps
`seq`. The submitter stores the op's arguments, inserts it into the pending queue sorted by
`FUN_0010c030` (key `prio << 24 | seq`, ascending; an op goes before the first queued op with an equal or greater
key), raises the worker's SIGNAL (`SetEvent(w+0x88)`) and returns the handle. Callbacks are attached afterwards
with `FILESYS_callbackop`.

**Worker loop** (`FUN_0010c110`): forever (the exit flag is never set) - lock pending; pop the head into `w+0x10`;
if its priority is above the ceiling, re-insert it and treat the queue as empty; unlock; if nothing, wait on the
SIGNAL (`INFINITE`) and loop. Otherwise, unless the op is already flagged cancelled, execute it (type table 2.3),
setting status and error. Then, under the pending lock, push the op on the **front** of the done queue and clear
`w+0x10`; then, if a callback is set, set flag 8 and call `callback(handle, cancelled ? -1 : status, userData)`
**on the worker thread**; then `SetEvent(w+0xac)`.

**Find an op** (`opstatus`, `waitop`, `cancelop`, `callbackop`), all under the pending lock: it is "running" if
`w+0x10` is it, "pending" if in the pending queue, "done" if in the done queue, else unknown.

**Wait** (`FILESYS_waitop`): while the op is running or pending, wait: on the main thread
(`THREAD_iscurrent(0)`), `SYNCTASK_run(0)` then `THREAD_yield(1)` (sleep 1 ms) - so periodic tasks keep running
during synchronous loads; on any other thread, `WaitForSingleObject(w+0xac, INFINITE)` then `ResetEvent`. Then
return `FILESYS_opstatus`.

**Complete** (`FILESYS_completeop`): remove from the done queue, finish the type's work (close, register a big
file, free a big file), free an owned name, zero the handle and append the record to the free list. Returns the
type's result. No check that the op was found.

**Cancel** (`FILESYS_cancelop`, only reached from `ASYNCFILE_cancel`): types 0, 2, 3, 4, 6, 8, 9 can be
cancelled. A pending op is flagged, moved to the done queue and its callback fired at once with status -1, on the
caller's thread. A running op is only flagged: it finishes, reports -1, and `completeop` closes what an open
opened. Done ops are left alone.

**Reprioritise** (`FILESYS_priorityop`, from STREAM): a pending op is taken out, given the new priority and
re-inserted (so it gets a new place but keeps its sequence), and the SIGNAL raised.

**Atomic** (`FILESYS_atomic(fn, worker, prio, arg)`): on the caller's thread, under `w+0x8c`, if
`prio <= ceiling`, set `ceiling = prio`, run `fn(prio, arg)`, restore the ceiling and raise the SIGNAL. While it
runs, the worker will not *start* an op of lower priority (a larger number); one already running is not
interrupted. Every FILE_* helper runs its open/read/close sequence this way with `prio = sync_priority` (100):
the open goes at priority 100, the rest at 99, so they overtake ordinary traffic, and the MUTEX makes concurrent
FILE_* calls from several threads take turns for the whole load. `fn` returns in `EAX`, passed through.

**Chunked sync read/write** (`FILESYS_readsync` / `writesync` through `FUN_0010b5f0`): the transfer is split into
0x2000-byte ops. The first is submitted by the caller; each completion callback (`0x0010b550`, worker thread)
completes the finished op, advances, and submits the next, until a short transfer, an error or the end. The
caller meanwhile loops `waitop(current)` until nothing remains and no op is current.

**Shutdown.** There is none for FILESYS: no restore, the worker threads run until the process ends, the block is
never freed. `ASYNCFILE_restore` cancels its requests, waits for their ops (running `SYNCTASK_run` on the main
thread), frees the table and destroys its MUTEX.

**Locks, in the order they nest:** `w+0x8c` (atomic) > free list `0x00242874` (only `FUN_0010cae0`) > pending
queue `w+0x14` > done queue `w+0x4c` / big list `0x002428d4` / slot table `0x00242a10`. Callbacks run with no
FILESYS lock held, except those `cancelop` and `callbackop` fire, which run under the pending lock.

---------------------------------------------------------------------------------------------------------------

## 4. Function reference

Size is bytes to the next function (Ghidra's sizes are wrong for several functions that run into undefined code;
the true end is given where it differs). "Callers outside" lists callers outside this layer; "internal" callers are
in the pseudocode. Every pseudocode `lock(m)`/`unlock(m)` is `MUTEX_lock`/`MUTEX_unlock` (`0x0014a520`/`0x0014a530`,
cdecl, RealSystem.cpp).

### 4.1 The locked queue, `0x0014a540..0x0014aa0f`

| address | name | size | convention | locks? | callers outside |
|---|---|---|---|---|---|
| `0x0014a540` | `FUN_0014a540` *(invented: Q_keyidentity)* | 0x10 | cdecl `(x) -> x` | - | `FUN_000a2fc0` (render), vtables at `0x001d4840`/`0x001d4870`. **Keep it**: it is shared code |
| `0x0014a550` | `FUN_0014a550` *(Q_unlink)* | 0xa0 | **`ECX` = q, `ESI` = node**, no stack args, returns `EAX` | no | none |
| `0x0014a5f0` | `FUN_0014a5f0` *(Q_init)* | 0x50 | cdecl `(q, keyFn, keyArg)` | creates | none |
| `0x0014a640` | `FUN_0014a640` *(Q_initshared)* | 0x50 | cdecl `(q, keyFn, keyArg, qFrom)` | copies | none |
| `0x0014a690` | `FUN_0014a690` *(Q_pushfront)* | 0x40 | cdecl `(q, node)` | yes | none |
| `0x0014a6d0` | `FUN_0014a6d0` *(Q_pushback)* | 0x50 | cdecl `(q, node)` | yes | none |
| `0x0014a720` | `FUN_0014a720` *(Q_popfront)* | 0x50 | cdecl `(q) -> node` | yes | none |
| `0x0014a770` | `FUN_0014a770` *(Q_insertsorted)* | 0x90 | cdecl `(q, node)` | yes | none |
| `0x0014a800` | `FUN_0014a800` *(Q_remove)* | 0x40 | cdecl `(q, node) -> bool` | yes | none |
| `0x0014a840` | `FUN_0014a840` *(Q_find)* | 0x50 | cdecl `(q, match, arg) -> node` | yes | none |
| `0x0014a890` | `FUN_0014a890` *(Q_findremove)* | 0x80 | cdecl `(q, match, arg) -> node` | yes | none |
| `0x0014a910` | `FUN_0014a910` *(Q_foreach)* | 0xa0 | cdecl `(q, fn, arg) -> int` | around the flag only | none |
| `0x0014a9b0` | `FUN_0014a9b0` *(Q_foreachlocked)* | 0x30 | cdecl `(q, fn, arg) -> int` | yes | none |
| `0x0014a9e0` | `FUN_0014a9e0` *(Q_lock)* | 0x20 | cdecl `(q) -> 0` | locks | none |
| `0x0014aa00` | `FUN_0014aa00` *(Q_unlock)* | 0x10 | cdecl `(q, ignored)` | unlocks | none |

`match`/`fn` are `int (*)(node*, arg)`, cdecl.

```c
int Q_unlink(q /*ECX*/, node /*ESI*/) {          // 0x0014a550, caller holds the lock
    if (!node || q->count == 0) return 0;
    if (node == q->head) {
        q->count--;
        if (node == q->tail) { q->head = q->tail = 0; } else q->head = node->next;
        node->next = 0; q->flags |= 1; return 1;
    }
    prev = q->head;                               // walk to the node before `node`
    while (prev->next && prev->next != node) prev = prev->next;
    if (prev->next != node) return 0;             // also when head->next was 0
    q->count--; prev->next = node->next;
    if (node == q->tail) q->tail = prev;
    node->next = 0; q->flags |= 1; return 1;
}
void Q_init(q, keyFn, keyArg) {                  // 0x0014a5f0
    MUTEX_create(&q->mutex);                      // +0x18
    q->count = 0; q->flags = 2; q->head = q->tail = 0;
    q->keyArg = keyArg; q->keyFn = keyFn ? keyFn : FUN_0014a540;
}
void Q_initshared(q, keyFn, keyArg, from) {      // 0x0014a640: copies from's 0x20 MUTEX bytes
    memcpy(&q->mutex, &from->mutex, 0x20);
    q->count = q->flags = 0; q->head = q->tail = 0;
    q->keyFn = keyFn ? keyFn : FUN_0014a540; q->keyArg = keyArg;
}
void Q_pushfront(q, n) { lock; if (n) { n->next = q->head; q->head = n; q->count++;
                         if (n->next == 0) q->tail = n; q->flags |= 1; } unlock; }
void Q_pushback(q, n)  { lock; if (n) { n->next = 0; q->count++; old = q->tail; q->tail = n;
                         if (old) old->next = n; else q->head = n; q->flags |= 1; } unlock; }
node *Q_popfront(q)    { lock; n = q->head;
                         if (n) { if (n == q->tail) q->head = q->tail = 0; else q->head = n->next;
                                  q->count--; n->next = 0; }
                         q->flags |= 1;              // even when empty
                         unlock; return n; }
void Q_insertsorted(q, n) {                       // 0x0014a770: ascending key, before equal keys
    lock;
    if (n) { k = q->keyFn(n, q->keyArg); q->count++;
             prev = 0; cur = q->head;
             while (cur && q->keyFn(cur, q->keyArg) < k) { prev = cur; cur = cur->next; }   // unsigned
             n->next = cur; if (prev) prev->next = n; else q->head = n;
             if (!cur) q->tail = n; q->flags |= 1; }
    unlock;
}
int  Q_remove(q, n)    { lock; r = n ? Q_unlink(q, n) : 0; unlock; return r; }
node *Q_find(q, m, a)  { lock; n = q->head; while (n && m && !m(n, a)) n = n->next; unlock; return n; }
node *Q_findremove(q, m, a) {                     // 0x0014a890
    lock; n = q->head;
    if (n) { while (m && !m(n, a)) { n = n->next; if (!n) { unlock; return 0; } }
             if (!Q_unlink(q, n)) n = 0; }
    unlock; return n;
}
int Q_foreach(q, fn, a) {                         // 0x0014a910
    count = 0;
    lock; saved = q->flags & 1; q->flags &= ~1; unlock;
    for (n = q->head; n && !(q->flags & 1); n = n->next, count++)
        if (!fn(n, a)) q->flags |= 1;             // fn returning 0 stops the walk
    lock; if (!(q->flags & 1)) count = -1;        // ran to the end: -1
          q->flags |= saved; unlock;
    return count;
}
int Q_foreachlocked(q, fn, a) { lock; r = Q_foreach(q, fn, a); unlock; return r; }
```

Note that any mutation during `Q_foreach` (which sets bit 0) also stops the walk.

### 4.2 Initialisation

| address | name | size | convention | callers outside |
|---|---|---|---|---|
| `0x0010cac0` | `FILESYS_setmemcallbacks` | 0x20 | cdecl `(alloc, free)` | `Bond_StartUpSystem` (`0x00114630`, `0x00114670`) |
| `0x0010ccc0` | `FILESYS_init` | 0x60 | cdecl `(numFiles, cdBuffer, numOps) -> bool (AL)` | `Bond_StartUpSystem` (0x10, 0x32, 0x20) |
| `0x0010c470` | `FILESYS_initadr` | 0xc0 | cdecl `(numFiles, cdBuffer, numOps, block) -> bool (AL)` | none |
| `0x0010d1f0` | `FUN_0010d1f0` *(PS2: FILEDEV_init)* | 0x30 | cdecl `(numFiles, unused, table)` | none |
| `0x0010d840` | `FUN_0010d840` *(invented: FILEDEV_setreaderror)* | 0x10 | cdecl `(handler)` | `FUN_0005c9a0` (engine.core) |

```c
bool FILESYS_init(numFiles, cdBuffer, numOps) {           // cdBuffer is the PS2 CD-DVD buffer: unused
    if (FILESYS_operations) return false;                 // returns FILESYS_operations & 0xffffff00
    f = numFiles ? numFiles : 0x10;  o = numOps ? numOps : 0x20;
    block = pFILE_malloc("File Sys", (o*3 + 0x17 + f) * 16, 0x100);   // no null check
    return FILESYS_initadr(numFiles, cdBuffer, numOps, block);
}
bool FILESYS_initadr(numFiles, cdBuffer, numOps, block) {
    if (!FILESYS_operations) {
        *(int *)0x0024290c = 0;
        f = numFiles ? numFiles : 0x10;  o = numOps ? numOps : 0x20;
        MEM_fill(block, 0, (o*3 + 0x17 + f) * 16);
        FUN_0010d1f0(f, 0, block);                         // slot table = block, count f
        Q_init(0x00242874, 0, 0);  Q_init(0x002428d4, 0, 0);
        FILESYS_operations = block + f*16;
        op = *(void **)0x002428cc = FILESYS_operations + 0x170;
        for (i = 0; i < o; i++, op += 0x30) Q_pushfront(0x00242874, op);
        MUTEX_create(0x002428ac);
        *(int *)0x002428d0 = -1;
    }
    return true;                                           // also when already initialised
}
void FUN_0010d1f0(n, unused, table) { MUTEX_create(0x00242a10); *(0x00242a34) = table; *(0x00242a30) = n; }
void FUN_0010d840(h) { *(0x001d1c14) = h; }
```

### 4.3 Workers and submission

| address | name | size | convention | callers outside |
|---|---|---|---|---|
| `0x0010cae0` | `FUN_0010cae0` *(invented: FILESYS_startworker)* | 0x110 | **`EBX` = worker index**, no stack args | none |
| `0x0010c110` | `FUN_0010c110` *(invented: FILESYS_workerthread)* | 0x360 | cdecl `(index)` (thread entry via `THREAD_createparam`) | none |
| `0x0010cbf0` | `FUN_0010cbf0` *(invented: FILESYS_allocop)* | 0xd0 | cdecl `(type, prio (byte), userData, worker) -> op*` | none |
| `0x0010c030` | `FUN_0010c030` *(invented: FILESYS_opkey)* | 0x20 (code to 0x0010c049) | cdecl `(op, unused) -> key` | none |
| `0x0010c010` | `FUN_0010c010` *(invented: FILESYS_ophandleis)* | 0x20 | cdecl `(op, handle) -> op->handle == handle` | none |
| `0x0010c050` | *(undefined; invented: FILESYS_bigidis)* | 0x15 | cdecl `(big, id) -> big->id == id` | none |

```c
void FILESYS_startworker(/*EBX*/ int i) {                 // 0x0010cae0
    w = FILESYS_operations + i*0xb8;
    if (w->alive) return;
    Q_lock(0x00242874);
    w->current = 0;
    Q_init(&w->pending, FUN_0010c030, 0);
    Q_initshared(&w->done, 0, 0, &w->pending);
    SIGNAL_create(&w->signal);                             // 0x0010a6e0
    w->doneEvent = CreateEventA(0, TRUE, FALSE, 0);        // manual reset, initially clear
    MUTEX_create(&w->atomicMutex);
    w->seq = 1; w->ceiling = 0xff;
    if (THREAD_createparam(&w->thread, FUN_0010c110, i, 0, 0, 1)) {
        WaitForSingleObject(FILESYS_operations->doneEvent /* worker 0's */, INFINITE);
        ResetEvent(FILESYS_operations->doneEvent);
    }
    Q_unlock(0x00242874, 0);
}

uint FILESYS_opkey(op, unused) { return (op->handle >> 5) & 0xffffff | op->prio << 24; }

op *FILESYS_allocop(type, prio, userData, i) {             // 0x0010cbf0
    op = Q_popfront(0x00242874);                           // no null check
    w = FILESYS_operations + i*0xb8;
    if (!w->alive) FILESYS_startworker(i);
    op->prio = (u8)prio; op->type = type; op->flags &= ~0xf;
    op->status = 0; op->error = 0; op->slot = 0; op->userData = userData;
    op->callback = 0; op->arg0 = op->arg1 = op->arg2 = 0;
    if (w->alive) Q_lock(&w->pending);
    op->handle = w->seq << 5 | i;
    w->seq = (w->seq + 1) & 0xffffff; if (!w->seq) w->seq = 1;
    if (w->alive) Q_unlock(&w->pending, 0);
    return op;
}

void FILESYS_workerthread(int i) {                          // 0x0010c110
    w = FILESYS_operations + i*0xb8;
    w->alive = 1; SetEvent(w->doneEvent);
    while (!*(int *)0x0024290c) {
        if (w->alive) Q_lock(&w->pending);
        op = w->current = Q_popfront(&w->pending);
        if (op && op->prio > w->ceiling) { Q_insertsorted(&w->pending, op); op = w->current = 0; }
        if (w->alive) Q_unlock(&w->pending, 0);
        if (!w->current) { SIGNAL_wait(&w->signal); continue; }   // 0x0010a710

        if (!(op->flags & 2)) switch (op->type) {
        case 0:  op->slot = FUN_0010c070(/*ESI*/ (char *)op->arg2, op->arg0, op->arg0 & 1);
                 if (op->slot) op->status = 1; else { op->status = -2; op->error = GetLastError(); }
                 break;
        case 1:  op->status = 1; break;
        case 2:  op->arg1 = FUN_0010d4a0(op->slot, op->arg2, op->arg0, op->arg1);
                 op->status = 1; op->error = GetLastError(); if (op->error) op->status = -2; break;
        case 3:  op->arg1 = FUN_0010d680(op->slot, op->arg2, op->arg0, op->arg1);
                 op->status = 1; op->error = GetLastError(); if (op->error) op->status = -2; break;
        case 4:  if (!op->slot) { op->arg0 = 0; goto fail; }
                 op->arg0 = FUN_0010d460(op->slot); op->status = 1; break;
        case 5: case 8: op->status = 1; op->error = 0; break;
        case 6:  s = FUN_0010c070(/*ESI*/ (char *)op->arg2, 1, 1); op->slot = s;
                 if (s) { FUN_0010d760(s); op->arg0 = 1; } else op->arg0 = 0;
                 op->status = 1; break;                    // "not found" is a success with 0
        case 7:  if (!FUN_0010d7d0((char *)op->arg2)) goto fail; op->status = 1; break;
        case 9:  big = op->arg2; if (!big) break;          // status stays 0
                 big->slot = FUN_0010c070(/*ESI*/ big->name, 1, 1);
                 if (!big->slot || FUN_0010d4a0(big->slot, hdr16, 0, 16) != 16) goto fail;
                 big->dirSize = FUN_0010ddf0(hdr16);
                 big->dir = pFILE_malloc("BF Header", big->dirSize, 0);       // no null check
                 MEM_copy(big->dir, hdr16, 16);
                 FUN_0010d4a0(big->slot, big->dir + 16, 16, big->dirSize - 16);  // result ignored
                 op->status = 1; break;
        case 10: Q_remove(0x002428d4, op->arg2); op->status = 1; break;
        fail:    op->status = -2; op->error = GetLastError(); break;
        }
        if (w->alive) Q_lock(&w->pending);
        Q_pushfront(&w->done, op); w->current = 0;
        if (w->alive) Q_unlock(&w->pending, 0);
        if (op->callback) { f = op->flags; op->flags = f | 8;
                            op->callback(op->handle, (f & 2) ? -1 : op->status, op->userData); }
        SetEvent(w->doneEvent);
    }
    w->alive = 0;
}
```

### 4.4 The asynchronous API

All cdecl, all return the op handle (or 0). `prio` is a byte. Every one but `FILESYS_size` ends with
`Q_insertsorted(&w->pending, op); SIGNAL_post(&w->signal); return op->handle;` where `w` is the op's worker
(`SIGNAL_post` = `iFILE_maybeExecCommand`, `0x0010a700`, `SetEvent(signal+4)`).

| address | name | true size | signature | fields set | callers outside |
|---|---|---|---|---|---|
| `0x0010cdc0` | `FILESYS_open` | 0xa0 | `(name, mode, prio, userData)` | type 0; `flags |= 1`; `arg2 = FILE_malloc("FileName", strlen+1, 0)` copy of name (null name = `""`); `arg0 = mode` | STREAM `FUN_0014af10`, `FUN_0014af50` |
| `0x0010ce60` | `FILESYS_close` | 0x50 | `(slot, prio, userData)` | type 1; `slot` | STREAM `FUN_0014af50` |
| `0x0010ceb0` | `FILESYS_read` | **0x64** (Ghidra says 0xe0: it runs into `FILESYS_write`) | `(slot, offset, buffer, count, prio, userData)` | type 2; `slot`, `arg0 = offset`, `arg1 = count`, `arg2 = buffer` | STREAM `FUN_0014b730` (tail-calls `callbackop` after) |
| `0x0010cf20` | *(undefined in Ghidra; PS2: FILESYS_write)* | 0x64 | `(slot, offset, buffer, count, prio, userData)` | type 3, as read | none (only `FILESYS_writesync`, by address) |
| `0x0010cf90` | `FILESYS_size` | 0xa0 | `(slot, prio, userData)` | type 4, **done here**: see below | none |
| `0x0010cd20` | `FILESYS_exists` | 0xa0 | `(name, prio, userData)` | type 6; name copied as for open | none |
| `0x0010d030` | `FILESYS_addbig` | 0xc0 | `(name, allocFlags, prio, userData)` | see below | none |
| `0x0010d0f0` | `FILESYS_delbig` | 0x70 | `(bigId, prio, userData)` | see below | none |

```c
uint FILESYS_size(slot, prio, userData) {                 // 0x0010cf90: never queued
    op = FILESYS_allocop(4, prio, userData, 0);
    w = FILESYS_operations;                                // worker 0, not op's
    if (!slot) { op->arg0 = 0; op->status = -2; op->error = GetLastError(); }
    else       { op->arg0 = FUN_0010d460(slot); op->status = 1; }
    if (w->alive) Q_lock(&w->pending);
    Q_pushfront(&w->done, op);
    w->current = 0;                                        // BUG: clobbers the worker's running op (7.2)
    if (w->alive) Q_unlock(&w->pending, 0);
    return op->handle;                                     // no callback, no SIGNAL
}
uint FILESYS_addbig(name, allocFlags, prio, userData) {
    big = pFILE_malloc("BigFile", 0x118, allocFlags);  if (!big) return 0;
    lock(0x002428ac); big->id = (*(int *)0x002428d0)--; unlock(0x002428ac);
    big->unused8 = big->slot = big->dir = big->dirSize = 0;
    strcpy(big->name, name);                               // unbounded, 0x100 available
    op = FILESYS_allocop(9, prio, userData, 0); op->arg2 = big;
    insert, post, return op->handle;
}
uint FILESYS_delbig(id, prio, userData) {
    big = Q_find(0x002428d4, 0x0010c050, id);  if (!big) return 0;
    op = FILESYS_allocop(10, prio, userData, 0); op->arg2 = big;
    insert, post, return op->handle;
}
```

| address | name | size | signature | callers outside |
|---|---|---|---|---|
| `0x0010c530` | `FILESYS_opstatus` | 0xb0 | `(handle) -> int` | none |
| `0x0010c860` | `FILESYS_waitop` | 0x120 | `(handle) -> int` | none (`FUN_0010b5f0` is in the layer) |
| `0x0010c700` | `FILESYS_completeop` | 0x160 | `(handle) -> int` | STREAM `FUN_0014aee0`, `FUN_0014af10`, `FUN_0014b660` |
| `0x0010c980` | `FILESYS_callbackop` | 0xd0 | `(handle, callback)` | STREAM `FUN_0014af10`, `FUN_0014af50`, `FUN_0014b730` (tail jump) |
| `0x0010c5e0` | `FILESYS_cancelop` | 0x120 | `(handle)` | none (only `ASYNCFILE_cancel`) |
| `0x0010ca50` | `FILESYS_priorityop` | 0x70 | `(handle, prio)` | `STREAM_setgreedystate` (tail jump), `STREAM_kill`, `FUN_0014aba0` |

```c
// shared lookup, inlined in each: returns where the op is; caller holds w->pending's lock
//   RUNNING (-1): w->current && w->current->handle == h
//   PENDING (1):  Q_find(&w->pending, FUN_0010c010, h)
//   DONE (0):     Q_find(&w->done,    FUN_0010c010, h)     (op = 0 if not there either)
int FILESYS_opstatus(h) {
    w = worker(h); if (w->alive) Q_lock(&w->pending);
    if (!w->alive) r = -3;
    else if (RUNNING or PENDING) r = 0;
    else if (DONE) r = (op->flags & 2) ? -1 : op->status;
    else r = -3;
    if (w->alive) Q_unlock(&w->pending, 0);
    return r;
}
int FILESYS_waitop(h) {
    w = worker(h);
    if (!h || !w->alive) return -3;
    for (;;) {
        busy = lookup is RUNNING or PENDING (and found);   // computed under the lock
        if (!busy) break;
        if (THREAD_iscurrent(0)) { SYNCTASK_run(0); THREAD_yield(1); }
        else { WaitForSingleObject(w->doneEvent, INFINITE); ResetEvent(w->doneEvent); }
    }
    return FILESYS_opstatus(h);
}
int FILESYS_completeop(h) {
    op = Q_findremove(&worker(h)->done, FUN_0010c010, h);  // no check: op 0 reads address 8
    switch (op->type) {
    case 0:  if ((op->flags & 2) && op->slot) { FUN_0010d760(op->slot); r = 0; }   // r = 0 (EBP)
             else r = op->slot; break;
    case 1:  r = FUN_0010d760(op->slot); break;
    case 2: case 3: r = op->arg1; break;
    case 4: case 6: r = op->arg0; break;
    case 8:  r = 0; break;
    case 9:  big = op->arg2; r = 0;
             if (big) { if (!(op->flags & 2) && op->status == 1) { Q_pushback(0x002428d4, big); r = big->id; }
                        else { if (big->slot) FUN_0010d760(big->slot); pFILE_mfree(big); } }
             break;                                       // on failure the directory buffer, if any, leaks
    case 10: big = op->arg2; r = 0;
             if (big) { pFILE_mfree(big->dir); FUN_0010d760(big->slot); pFILE_mfree(big); r = 1; }
             break;
    default: r = (op->status == 1); break;                // 5, 7
    }
    if (op->flags & 1) pFILE_mfree((void *)op->arg2);
    op->handle = 0;
    Q_pushback(0x00242874, op);
    return r;
}
void FILESYS_callbackop(h, cb) {
    w = worker(h); if (w->alive) Q_lock(&w->pending);
    if (w->alive && (op = lookup)) {
        op->flags |= 4;
        if (DONE) { op->flags |= 0xc; op->callback = 0; cb(op->handle, op->status, op->userData); }
        else op->callback = cb;                           // fires later, on the worker
    }
    if (w->alive) Q_unlock(&w->pending, 0);
}
void FILESYS_cancelop(h) {
    w = worker(h); if (w->alive) Q_lock(&w->pending);
    if (w->alive && (op = lookup) && where != DONE && op->type in {0,2,3,4,6,8,9}) {
        op->flags |= 2;
        if (Q_remove(&w->pending, op)) {                  // fails for the running op
            Q_pushfront(&w->done, op);
            if (op->callback) { op->flags |= 8; op->callback(op->handle, -1, op->userData); }
        }
    }
    if (w->alive) Q_unlock(&w->pending, 0);
}
void FILESYS_priorityop(h, prio) {
    op = Q_findremove(&worker(h)->pending, FUN_0010c010, h);   // pending only
    if (op) { op->prio = prio; Q_insertsorted(&worker(op->handle)->pending, op); SIGNAL_post(...); }
}
```

### 4.5 Synchronous wrappers

All cdecl. Each submits with `userData = 0`, `waitop`s, and `completeop`s.

| address | name | size | signature | returns | callers outside |
|---|---|---|---|---|---|
| `0x0010b690` | `FILESYS_opensync` | 0x60 | `(name, mode, prio, int *outSlot)` | AL: `opstatus == 1`; `*outSlot = completeop` (0 if submit failed) | `FileNameList::Dump` |
| `0x0010b750` | `FILESYS_closesync` | 0x40 | `(slot, prio)` | AL: `completeop != 0` | `TextFile::~TextFile`, `FileNameList::Dump`, `STREAM_destroy` |
| `0x0010b790` | `FILESYS_sizesync` | 0x40 | `(slot, prio)` | size | none |
| `0x0010b7d0` | `FILESYS_addbigsync` | 0x60 | `(name, allocFlags, prio, int *outId)` | AL: `opstatus == 1`; `*outId` = id | `Bond_StartUpSystem`, `UFileLoader::StartUsingBigFile`, `AStreamPriv::AStreamPriv` |
| `0x0010b830` | `FILESYS_delbigsync` | 0x40 | `(id, prio)` | AL: `completeop != 0` | `Bond_CleanUp`, `UFileLoader::StopUsingBigFile`, `AStreamPriv::~AStreamPriv` |
| `0x0010b870` | `FILESYS_existssync` | 0x40 | `(name, prio)` | AL: exists | `DTuningFile::DTuningFile` (2), `Bond_StartUpSystem`, `GameLoop_StartUp`, `AStreamPriv::AStreamPriv` |
| `0x0010b6f0` | `FILESYS_readsync` | 0x30 | `(slot, offset, buffer, count, prio)` | total bytes | none |
| `0x0010b720` | `FILESYS_writesync` | 0x30 | `(slot, offset, buffer, count, prio)` | total bytes | `FileNameList::Dump` |
| `0x0010b5f0` | `FUN_0010b5f0` *(invented: FILESYS_chunkedsync)* | 0xa0 | **one stack arg `slot` (caller pops 4); `EAX` = count, `EDX` = prio, `EDI` = offset, `ESI` = buffer, `ECX` = op function** | total bytes | none |
| `0x0010b550` | *(undefined; invented: FILESYS_chunkdone)* | 0x99 | cdecl `(handle, status, ctx)`: an op callback | - | none |

`readsync`/`writesync` load `EAX, EDX, EDI, ESI, ECX` (`ECX = 0x0010ceb0` / `0x0010cf20`), push `slot`, and call
`FUN_0010b5f0`; they are the only callers, so the register convention dies with them.

```c
struct ChunkCtx {            // on FUN_0010b5f0's stack
    int prio;                // +0x00
    int slot;                // +0x04
    int offset;              // +0x08
    int remaining;           // +0x0c
    int done;                // +0x10  bytes transferred so far (the result)
    int chunk;               // +0x14  size of the current op
    char *buffer;            // +0x18
    uint (*opfn)(slot, offset, buffer, count, prio, userData);   // +0x1c FILESYS_read or FILESYS_write
    uint op;                 // +0x20  current op handle
};
int FILESYS_chunkedsync(slot, count, prio, offset, buffer, opfn) {
    ChunkCtx c = { prio, slot, offset, count, 0, count, buffer, opfn };
    if (c.chunk > 0x2000) c.chunk = 0x2000;
    c.op = opfn(slot, offset, buffer, c.chunk, prio, &c);
    if (c.op) {
        FILESYS_callbackop(c.op, FILESYS_chunkdone);      // may run it at once
        do FILESYS_waitop(c.op); while (c.remaining || c.op);   // waitop(0) returns -3 at once
    }
    return c.done;
}
void FILESYS_chunkdone(h, status, ChunkCtx *c) {          // worker thread (or caller, via callbackop)
    n = FILESYS_completeop(h); c->op = 0;
    if (status != 1) { c->remaining = 0; return; }
    c->offset += n; c->done += n; c->buffer += n;
    if (n < c->chunk) c->remaining = 0; else c->remaining -= n;
    if (c->remaining > 0) {
        c->chunk = min(c->remaining, 0x2000);
        c->op = c->opfn(c->slot, c->offset, c->buffer, c->chunk, c->prio, c);
        if (c->op) { FILESYS_callbackop(c->op, FILESYS_chunkdone); return; }
        c->remaining = 0;
    }
}
```

### 4.6 FILE_* helpers

Each packs a request block on its stack and calls `FILESYS_atomic(fn, 0, sync_priority (100), &block)`; `fn`
then runs `opensync(path, mode, prio)`, `sizesync`/`readsync`/`writesync`/`closesync` at `prio - 1`.

```c
struct FileReq { char *path; void *buffer; int size; int zflag; int allocFlags; };   // 0x14, invented name
```

`zflag` (+0xc) is 1 for the non-`z` variants and 0 for the `z` ones; nothing reads it on Xbox.

| address | name | true size | signature | block | atomic fn | callers outside |
|---|---|---|---|---|---|---|
| `0x0010d160` | `FILESYS_atomic` | 0x90 | cdecl `(fn, worker, prio, arg) -> fn's EAX` | | | none |
| `0x0010d850` | `FILE_exists` | 0x20 | `(path) -> bool` | - (calls `existssync(path, 100)`) | | `WTriggerManager::Restart` (2), `UFileLoader::StartUsingBigFile`, `::AttemptBigFileExists`, `::FileExists` |
| `0x0010d8c0` | `FILE_sizez` | 0x40 | `(path) -> size` | path, size=0 | `0x0010d870` | `UFileLoader::AttemptBigFileSize`, `::FileSize` |
| `0x0010d870` | `FUN_0010d870` *(PS2: FILE_size_internal-alike, invented)* | 0x50 | `(prio, FileReq*) -> size` | | | none |
| `0x0010d980` | `FILE_load` | 0x40 | `(path, allocFlags) -> void*` | path, zflag=1, allocFlags | `0x0010d900` | ours (`UFileLoader`, through AUTOGEN) |
| `0x0010d9c0` | `FILE_loadz` | **0x40** (Ghidra: 0xd0, runs into `0x0010da00`) | `(path, allocFlags) -> void*` | path, zflag=0 | `0x0010d900` | ours |
| `0x0010d900` | `FILE_load_internal` | 0x80 | `(prio, FileReq*) -> void*` | | | none |
| `0x0010da90` | `FILE_loadsizez` | 0x50 | `(path, int *outSize, allocFlags) -> void*` | path, zflag=0 | `0x0010da00` | none (only `FILE_loadpackz`) |
| `0x0010da00` | *(undefined; PS2: FILE_loadsize_internal)* | 0x85 | `(prio, FileReq*) -> void*`, stores size in `req->size` | | | none |
| `0x0010db40` | `FILE_loadat` | 0x50 | `(path, buffer, size) -> bytes` | path, buffer, size, zflag=1 | `0x0010dae0` | `UFileLoader::FileLoadAt` |
| `0x0010db90` | `FILE_loadatz` | 0x50 | `(path, buffer, size) -> bytes` | zflag=0 | `0x0010dae0` | none (only `FILE_unpacksizez`) |
| `0x0010dae0` | `FILE_loadat_internal` | 0x60 | `(prio, FileReq*) -> bytes` | | | none |
| `0x0010dc40` | `FILE_save` | 0x50 | `(path, buffer, size) -> bool` | zflag=1 | `0x0010dbe0` | `RRenderer::DoScreenCapture` |
| `0x0010dbe0` | `FUN_0010dbe0` *(invented: FILE_save_internal)* | 0x60 | `(prio, FileReq*) -> bool` | | | none |
| `0x0014aa90` | `FILE_unpacksizez` | 0x40 | `(path) -> unpacked size` | | | `UFileLoader::AttemptBigFileSize`, `::FileSize` |
| `0x0014aad0` | `FILE_loadpackz` | 0xd0 | `(path, allocFlags) -> void*` | | | ours (`UFileLoader::AttemptBigFileLoad`), `FUN_0014a370` (sound) |

```c
int FILESYS_atomic(fn, i, prio, arg) {
    r = 0;
    if (i < 0 || i > 0x1f) return 0;
    w = FILESYS_operations + i*0xb8;
    if (!w->alive) FILESYS_startworker(/*EBX*/ i);
    lock(&w->atomicMutex);
    old = w->ceiling;
    if (prio <= old) { w->ceiling = prio; r = fn(prio, arg); w->ceiling = old; SIGNAL_post(&w->signal); }
    unlock(&w->atomicMutex);
    return r;                                               // 0 without calling fn if prio > ceiling
}
int FUN_0010d870(prio, FileReq *r) {                        // size
    if (!FILESYS_opensync(r->path, 1, prio, &s)) return 0;
    n = FILESYS_sizesync(s, prio-1); FILESYS_closesync(s, prio-1); return n;
}
void *FILE_load_internal(prio, FileReq *r) {
    if (!FILESYS_opensync(r->path, 1, prio, &s)) return 0;
    n = FILESYS_sizesync(s, prio-1);
    buf = pFILE_malloc(r->path, n, r->allocFlags);         // the path is the allocation's name
    if (buf) FILESYS_readsync(s, 0, buf, n, prio-1);       // short read not detected
    FILESYS_closesync(s, prio-1);
    return buf;
}
void *FILE_loadsize_internal(prio, FileReq *r) {           // 0x0010da00: same, and r->size = n on success
}
int FILE_loadat_internal(prio, FileReq *r) {
    n = r->size > 0 ? r->size : 0x7fffffff;
    if (!FILESYS_opensync(r->path, 1, prio, &s)) return 0;
    got = FILESYS_readsync(s, 0, r->buffer, n, prio-1); FILESYS_closesync(s, prio-1); return got;
}
bool FILE_save_internal(prio, FileReq *r) {                 // 0x0010dbe0
    if (!FILESYS_opensync(r->path, 6, prio, &s)) return false;
    n = FILESYS_writesync(s, 0, r->buffer, r->size, prio-1); FILESYS_closesync(s, prio-1);
    return n == r->size;
}
bool FILE_save(path, buf, size) { return FILESYS_atomic(FILE_save_internal, 0, 100, &req) == 1; }
void *FILE_loadsizez(path, int *outSize, flags) { p = atomic(...); if (outSize) *outSize = req.size; return p; }
                                                          // req.size stays 0 on failure

uint FILE_unpacksizez(path) {
    u8 head[0x80];
    if (!FILE_loadatz(path, head, 0x80)) return 0;
    return unpacksizez(head);                              // 0x0014c0e0 (platform.movie: EA's packer)
}
void *FILE_loadpackz(path, flags) {
    p = FILE_loadsizez(path, &n, flags); if (!p) return 0;
    u = unpacksizez(p);
    if (n && u) {                                          // packed: copy, free, unpack
        tmp = pFILE_malloc(path, n, flags ^ 0x100);
        if (!tmp) { pFILE_mfree(p); return 0; }
        MEM_copy(tmp, p, n); pFILE_mfree(p);
        p = pFILE_malloc(path, u, flags);
        if (p && !FUN_0014bff0(tmp, p)) { pFILE_mfree(p); p = 0; }   // the unpacker
        pFILE_mfree(tmp);
    }
    return p;                                              // not packed: the raw file
}
```

### 4.7 Path resolution and opening

| address | name | size | convention | callers outside |
|---|---|---|---|---|
| `0x0010c070` | `FUN_0010c070` *(invented: FILEDEV_resolveopen)* | 0xa0 (code to 0x0010c10f) | **`ESI` = path**; stack `(mode, searchBig)`; caller pops 8; returns slot handle | none |
| `0x0010bf60` | *(undefined; invented: FILEDEV_bigmatch)* | 0xa2 | cdecl `(big, ctx) -> int` (a `Q_foreach` callback) | none |
| `0x0010d2f0` | `FUN_0010d2f0` *(PS2: FILEDEV_open)* | 0x170 | cdecl `(path, mode, int *outSlot) -> bool` | none |
| `0x0010d220` | `FUN_0010d220` *(invented: FILEDEV_claim)* | 0x70 | no arguments, returns index or -1 | none |
| `0x0010d290` | `FUN_0010d290` *(invented: FILEDEV_opensub)* | 0x60 | cdecl `(archiveSlot, base, size, int *outSlot) -> bool` | none |

Ghidra folds `0x0010bf60` (and the padding before it) into `ASYNCFILE_restore`, whose code really ends at
`0x0010bf52`.

```c
struct ResolveCtx { char *bigName; const char *sub; int slot; int found; };   // on 0x0010c070's stack

int FILEDEV_resolveopen(/*ESI*/ const char *path, int mode, int searchBig) {
    char name[256]; ResolveCtx c = { name, path, 0, 0 };
    bar = strchr(path, '|');                               // 0x00133a30
    if (bar) { strncpy(name, path, bar - path); name[bar - path] = 0; c.sub = bar + 1; }   // no bounds
    else     { c.found = FUN_0010d2f0(path, mode, &c.slot); name[0] = 0; }
    if (searchBig && !c.found) Q_foreachlocked(0x002428d4, FILEDEV_bigmatch, &c);
    return c.slot;
}
// "a|b": only the big file registered as "a", not the disk; "|b": every big file, not the disk;
// "b": the disk, then (mode & 1 only) every big file. Big files are searched in registration order.

int FILEDEV_bigmatch(big, ResolveCtx *c) {               // returns 0 to stop
    anyBig = (c->bigName[0] == 0);
    if (!anyBig && strcmp(big->name, c->bigName) != 0) return 1;   // case-sensitive, as given to addbig
    if (FUN_0010de40(big->dir, c->sub, 0, &off, &size)) {
        c->found = FUN_0010d290(big->slot, off, size, &c->slot);
        return 0;
    }
    return anyBig;                                         // the named big file lacks it: stop
}

int FILEDEV_claim(void) {                                 // 0x0010d220
    lock(0x00242a10);
    for (i = 0; i < count; i++) if (table[i].handle == 0) { table[i].handle = 1; unlock; return i; }
    unlock; return -1;
}
bool FILEDEV_opensub(archiveSlot, base, size, int *out) {
    i = FILEDEV_claim(); *out = 0; if (i < 0) return false;
    table[i] = { table[~archiveSlot].handle, 0 /*pos*/, size, base };
    *out = ~i; return true;
}
bool FILEDEV_open(path, mode, int *out) {                 // 0x0010d2f0
    char full[256];
    i = FILEDEV_claim(); *out = 0; SetLastError(0);
    if (i < 0) return false;                               // no free slot: fails with error 0
    access = (mode & 1) ? GENERIC_READ : GENERIC_READ|GENERIC_WRITE;
    share  = (mode & 1) ? FILE_SHARE_READ : 0;
    disp   = (mode & 2) ? ((mode & 4) ? CREATE_ALWAYS : CREATE_NEW)
                        : ((mode & 4) ? TRUNCATE_EXISTING : OPEN_EXISTING);
    strcpy(full, path);
    if (path[1] != ':') {
        if (*(char *)0x00242910 == 0) sprintf(full, "%s\\%s", root /*"D:"*/, path);
        else sprintf(full, "%s\\%s\\%s", root, (char *)0x00242910, path);   // never taken
    }
    for (p = full; *p; p++) if (*p == '/') *p = '\\';
    h = CreateFileA(full, access, share, 0, disp, FILE_ATTRIBUTE_NORMAL, 0);
    table[i].handle = h;
    if (h == INVALID_HANDLE_VALUE) { table[i].handle = 0; return false; }   // error from CreateFileA
    table[i].size = GetFileSize(h, 0);                     // pos and base stay 0 (zero-filled / cleared)
    *out = ~i; return true;
}
```

### 4.8 Slot I/O

| address | name | size | signature (cdecl) | callers outside |
|---|---|---|---|---|
| `0x0010d460` | `FUN_0010d460` *(PS2: FILEDEV_getsize)* | 0x40 | `(slot) -> size` | none |
| `0x0010d4a0` | `FUN_0010d4a0` *(PS2: FILEDEV_readreq)* | 0x1e0 | `(slot, buffer, offset, count) -> bytes read` | none |
| `0x0010d680` | `FUN_0010d680` *(PS2: FILEDEV_write)* | 0xe0 | `(slot, buffer, offset, count) -> bytes written` | none |
| `0x0010d760` | `FUN_0010d760` *(PS2: FILEDEV_close)* | 0x70 | `(slot) -> bool` | none |
| `0x0010d7d0` | `FUN_0010d7d0` *(invented: FILEDEV_delete)* | 0x70 | `(path) -> DeleteFileA's BOOL in EAX` (Ghidra says void) | none |

`offset` is relative to the start of the (sub)file. Positions in the slot are absolute.

```c
int FILEDEV_read(slot, buf, offset, count) {
    if (!valid(slot)) { SetLastError(6); return 0; }
    SetLastError(0);
    s = &table[~slot];
    if (s->base == 0) {                                    // loose file
        if (s->pos != offset) {
            s->pos = clamp(offset, 0, s->size);            // > size -> size, < 0 -> 0
            s->pos = SetFilePointer(s->handle, s->pos, 0, FILE_BEGIN);
        }
        if (s->pos + count > s->size) count = s->size - s->pos;
    } else {                                               // inside a big file
        if (s->base - s->pos != offset) {                  // sic: almost always true, see 7.3
            s->pos = clamp(s->base + offset, s->base, s->base + s->size);
            s->pos = SetFilePointer(s->handle, s->pos, 0, FILE_BEGIN);
        }
        if (s->pos - s->base + count > s->size) count = s->size - s->pos + s->base;
    }
    if (!ReadFile(s->handle, buf, count, &got, 0) && readErrorHandler && !readErrorHandler())
        Sleep(INFINITE);                                   // 0x0010e9ab(-1)
    s->pos += got; clamp s->pos to the (sub)file's end;
    return got;
}
int FILEDEV_write(slot, buf, offset, count) {              // loose files only (base ignored)
    if (!valid(slot)) { SetLastError(6); return 0; }
    SetLastError(0); s = &table[~slot];
    if (s->pos != offset) { s->pos = clamp(offset, 0, s->size); s->pos = SetFilePointer(...); }
    WriteFile(s->handle, buf, count, &put, 0);             // result ignored
    s->pos += put; if (s->pos > s->size) s->size = s->pos;
    return put;
}
int FILEDEV_getsize(slot) { if (!valid) { SetLastError(6); return 0; } return table[~slot].size; }
bool FILEDEV_close(slot) {
    if (!valid) { SetLastError(6); return false; }
    SetLastError(0); s = &table[~slot];
    if (s->base == 0 && s->handle != INVALID_HANDLE_VALUE) CloseHandle(s->handle);
    *s = { 0, 0, 0, 0 }; return true;
}
BOOL FILEDEV_delete(path) {
    SetLastError(0);
    if (path[1] != ':') { sprintf(full, "%s\\%s", root, path); return DeleteFileA(full); }
    strcpy(full, path); return DeleteFileA(full);          // no '/' conversion here
}
```

### 4.9 Big-file directory helpers

All are called only by each other, by the worker (addbig) and by `FILEDEV_bigmatch`.

| address | name | size | convention |
|---|---|---|---|
| `0x0010dd80` | `FUN_0010dd80` *(invented: BIG_kind)* | 0x70 | cdecl `(hdr) -> 0..3` (2.6) |
| `0x0010ddf0` | `FUN_0010ddf0` *(invented: BIG_dirsize)* | 0x50 | cdecl `(hdr) -> size` (2.6) |
| `0x0010de40` | `FUN_0010de40` *(invented: BIG_find)* | 0x1a0 | cdecl `(dir, name, index, int *outOffset, int *outSize) -> entry name or 0` |
| `0x0010dc90` | `FUN_0010dc90` *(invented: BIG_readbe)* | 0x50 | **`EAX` = bytes, `ECX` = width 1..4**, returns BE value (0 for other widths) |
| `0x0010dd30` | `FUN_0010dd30` *(invented: BIG_readbe64)* | 0x50 | **`EAX` = width, `EDI` = bytes**, returns `EDX:EAX` |
| `0x0010dce0` | `FUN_0010dce0` *(invented: BIG_stricmp)* | 0x50 | **`ECX` = a, `EAX` = b**, returns `toupper(a[i]) - toupper(b[i])` at the first difference |

```c
char *BIG_find(dir, name, index, int *outOff, int *outSize) {
    n = 0; footer = 0;
    t = dir + BIG_dirsize(dir) - 8;
    if (isalpha_ascii(t[0]) && isdigit(t[1]) && isdigit(t[2]) && isdigit(t[3])) footer = 8;
    offW = 4; sizeW = 4; p = dir + 0x10;
    end = dir + BIG_dirsize(dir) - footer;
    switch (BIG_kind(dir)) {
    case 1: offW = sizeW = 3; p = dir + 6; break;
    case 3: offW = dir[3] - '0'; sizeW = min(offW, 4); break;
    }
    for (; p < end; p = e + strlen(e) + 1) {
        e = p + offW + sizeW;                              // the entry's name
        if (name ? BIG_stricmp(e, name) == 0 : n++ == index) {
            if (outOff)  *outOff  = (int)BIG_readbe64(offW, p);
            if (outSize) *outSize = BIG_readbe(p + offW, sizeW);
            return e;
        }
    }
    if (outOff) *outOff = 0; if (outSize) *outSize = 0;
    return 0;
}
```

The index form (`name == 0`) has no caller on Xbox (*PS2: FILESYS_bypassqueuefileinfo*-style listing).

### 4.10 ASYNCFILE

Request record (0x30) *(invented layout names)*: +0 id (`generation | index`, index in the low byte), +4 free-list
link, +8 bytes done, +0xc released, +0x10 cancelled, +0x14 buffer (1 = "allocate one"), +0x18 callback
`void (*)(int id)`, +0x1c current FILESYS op, +0x20 slot, +0x24 offset, +0x28 remaining (allocation flags
until the size is known), +0x2c write pointer. FILESYS ops are submitted at priority 99 (the open at 100).

| address | name | size | convention | callers outside | live? |
|---|---|---|---|---|---|
| `0x0010bbb0` | `ASYNCFILE_init` | 0x90 | cdecl `(count, allocFlags)` | `Bond_StartUpSystem` (0x14, 0) | live |
| `0x0010bea0` | `ASYNCFILE_restore` | 0xb3 (Ghidra 0x170) | cdecl `()` | `Bond_CleanUp` | live |
| `0x0010bde0` | `ASYNCFILE_cancel` | 0xc0 | cdecl `(id) -> 1 / -1` | none | live (from restore) |
| `0x0010b8b0` | `FUN_0010b8b0` *(invented: ASYNCFILE_free)* | 0x70 | **`ESI` = record**, no stack args | none | live |
| `0x0010bc40` | `ASYNCFILE_load` | 0xba | cdecl `(name, allocFlags) -> id` | `RCMP::AV_PLAYER::Init` | dead |
| `0x0010bd00` | `ASYNCFILE_release` | 0xd8 | cdecl `(id, void **outBuf, int *outSize) -> 1 / -1` | `RCMP::AV_PLAYER::GetFirstFrame` | dead |
| `0x0010b920` | `FUN_0010b920` *(ASYNCFILE_finish)* | 0x50 | **`EAX` = record** | none | dead |
| `0x0010b970` | `FUN_0010b970` *(ASYNCFILE_closedone)* | 0x20 | op callback `(h, status, rec)` | none | dead |
| `0x0010b990` | `FUN_0010b990` *(ASYNCFILE_readdone)* | 0xb0 | op callback | none | dead |
| `0x0010ba40` | `FUN_0010ba40` *(ASYNCFILE_sizedone)* | 0xa0 | op callback | none | dead |
| `0x0010bae0` | `FUN_0010bae0` *(ASYNCFILE_opendone)* | 0xd0 | op callback | none | dead |

The coverage tool counts the dead ones dead already (the movie player that called them is ours,
`src/driving/engine/PlayMPC.cpp`). Live pseudocode:

```c
int ASYNCFILE_init(count, flags) {                        // returns junk in EAX; nobody reads it
    if (request || count > 0x100) return ...;
    numrequests = count;
    request = freeHead = pFILE_malloc("ASYNCFILE", count*0x30, flags);   // no null check
    freeTail = request + (count-1)*0x30;
    MUTEX_create(0x00242838);
    for (i = 0; i < count; i++) { r = request + i*0x30; r->id = i; r->buffer = 0; r->link = r + 0x30; }
    last->link = 0;
}
void ASYNCFILE_free(/*ESI*/ r) {                          // 0x0010b8b0
    if (r->cancelled && r->buffer > 1) pFILE_mfree(r->buffer);
    r->id &= 0xff; r->op = 0;
    lock(0x00242838);
    if (freeHead) freeTail->link = r; else freeHead = r;
    freeTail = r; r->link = 0;
    unlock(0x00242838);
}
int ASYNCFILE_cancel(id) {
    lock(0x00242838);
    r = (id >= 0x100 && (id & 0xff) < numrequests && request[id & 0xff].id == id) ? &request[id & 0xff] : 0;
    if (r) { op = r->op; was = r->cancelled; rel = r->released;
             if (op || !rel) r->cancelled = 1; }
    unlock(0x00242838);
    if (r && !was && r->cancelled) { if (op) FILESYS_cancelop(op); else if (!rel) ASYNCFILE_free(r); return 1; }
    return -1;
}
void ASYNCFILE_restore(void) {
    if (!request) return;
    for (i = 0; i < numrequests; i++) ASYNCFILE_cancel(request[i].id);
    while (any request[i].op != 0) { if (THREAD_iscurrent(0)) SYNCTASK_run(0); THREAD_yield(0); }
    pFILE_mfree(request); REALMUTEX_destroy(0x00242838); request = 0;
}
```

Because nothing can submit an ASYNCFILE request any more, `ASYNCFILE_restore`'s cancels all find free records
(`r->id` is a bare index < 0x100, so validation fails) and the wait loop exits at once. A port only has to keep
`init` allocating and `restore` freeing the table and destroying the MUTEX.

### 4.11 STREAM helpers that touch FILESYS (movie/sound streaming)

The STREAM code (`0x0014acc0..0x0014be60`, `platform.movie`) is the sound streamer's back end
(`SNDSTRMI_queue` -> `STREAM_queuefile`, used for music and speech) and the main asynchronous FILESYS client: it
opens with `FILESYS_open(name, 1, stream+0x50 (priority), stream)`, reads with `FILESYS_read`, completes in its
callbacks (`FUN_0014aee0`, `FUN_0014af10`, `FUN_0014b660`), reprioritises with `FILESYS_priorityop`, and closes
with `FILESYS_close`/`FILESYS_closesync`. The three helpers named in the brief are STREAM internals with register
arguments; they belong with a STREAM port, not this one, and only the first calls FILESYS.

| address | name | size | convention | callers |
|---|---|---|---|---|
| `0x0014aba0` | `FUN_0014aba0` *(invented: STREAM_consumed)* | 0x60 | **`ESI` = stream**; stack `(bytes)`, caller pops | `STREAM_release`, `STREAM_cancelrequest` |
| `0x0014ac00` | `FUN_0014ac00` *(invented: STREAM_allocrequest)* | 0x70 | **`EDI` = stream**, returns request | `STREAM_queuefile`, `STREAM_queuemem` |
| `0x0014ac70` | `FUN_0014ac70` *(invented: STREAM_appendrequest)* | 0x50 | **`EDI` = stream, `ESI` = request** | `STREAM_queuefile`, `STREAM_queuemem` |

```c
void STREAM_consumed(/*ESI*/ s, int bytes) {               // stream: +4 MUTEX, +0x48 state, +0x50 greedy
    lock(s+4); old = s->buffered /*+0x5c*/; s->buffered = old - bytes; unlock(s+4);   // priority, +0x54
    if (old >= s->lowWater /*+0x54*/ && s->buffered < s->lowWater) {                   // low water, +0x58 hungry
        s->hungry = 1;
        if (s->state == 1) FILESYS_priorityop(s->op /*+0x184*/, s->greedyPrio /*+0x50*/);
    }
}
request *STREAM_allocrequest(/*EDI*/ s) {                  // free list at +0x78, link at request +0xc
    lock(s+4); r = s->freeRequests; if (!r) { unlock; return 0; }
    s->freeRequests = r->next;
    gen = *(0x002475fc) += 0x100; if (!gen) gen = *(0x002475fc) = 0x100;
    r->id = (r->id & 0xff) | gen; unlock(s+4); return r;
}
void STREAM_appendrequest(/*EDI*/ s, /*ESI*/ r) {          // list head +0x6c, current +0x70, tail +0x74
    r->state /*+4*/ = 1; r->next /*+0xc*/ = 0;
    lock(s+4);
    if (!s->tail) { r->prev /*+8*/ = 0; s->head = s->current = s->tail = r; }
    else { r->prev = s->tail; s->tail->next = r; s->tail = r; }
    unlock(s+4);
}
```

### 4.12 Out of scope, though nearby or listed

| address | name | why |
|---|---|---|
| `0x0010a700` | `iFILE_maybeExecCommand` (really REAL's `SIGNAL_post`: `SetEvent(sig->event)`) | REAL SIGNAL library; also called by `IFeedback` (engine.input). Port with `RealSystem.cpp`, keep the address patched |
| `0x0010a710` | `FUN_0010a710` (REAL's `SIGNAL_wait`: `WaitForSingleObject(sig->event, INFINITE)`) | same; also `IFeedback::Update` |
| `0x0014aa10` | `FUN_0014aa10` | `IsBadReadPtr`/`IsBadWritePtr` wrapper, called by `FUN_00113b10` |
| `0x0014aa50` | `MEM_move` | memory library |
| `0x0010dfe0`, `0x0010e002`, `0x0010e02b` | `ExQueryNonVolatileSetting` wrapper, `XGetAVPack`- and `XGetVideoFlags`-alikes | XAPI, misfiled under `platform.files` by the address range |
| `0x0014c0e0`, `0x0014bff0` | `unpacksizez`, the unpacker | EA's compression (`platform.movie`); called from `FILE_unpacksizez`/`FILE_loadpackz` |

### 4.13 What becomes dead

Once these entry points are replaced, **everything else in sections 4.1-4.10 is dead**:

- called from outside: `FILESYS_setmemcallbacks`, `FILESYS_init`, `ASYNCFILE_init`, `ASYNCFILE_restore`,
  `FILESYS_addbigsync`, `FILESYS_delbigsync`, `FILESYS_existssync`, `FILESYS_opensync`, `FILESYS_writesync`,
  `FILESYS_closesync`, `FILE_exists`, `FILE_sizez`, `FILE_unpacksizez`, `FILE_loadat`, `FILE_save`,
  `FILE_loadpackz`, `FUN_0010d840`;
- called by our code (AUTOGEN in `src/driving/platform/FILE.cpp`): `FILE_load`, `FILE_loadz`, `FILE_loadpackz`;
- called by STREAM: `FILESYS_open`, `FILESYS_close`, `FILESYS_read`, `FILESYS_completeop`, `FILESYS_callbackop`,
  `FILESYS_priorityop`, `FILESYS_closesync`.

If STREAM is replaced in the same change those last six need not be patched. `FUN_0014a540` stays (shared). The
queue helpers have no outside caller and die with FILESYS.

The register-argument functions and their callers, which must therefore go together:
`FUN_0014a550` (`ECX`, `ESI`: `Q_remove`, `Q_findremove`), `FUN_0010cae0` (`EBX`: `FUN_0010cbf0`,
`FILESYS_atomic`), `FUN_0010b5f0` (`EAX`, `EDX`, `EDI`, `ESI`, `ECX`: `readsync`, `writesync`), `FUN_0010c070`
(`ESI`: the worker), `FUN_0010b8b0` (`ESI`: `ASYNCFILE_cancel` and the dead ASYNCFILE code), `FUN_0010dc90`,
`FUN_0010dd30`, `FUN_0010dce0` (`BIG_find`), plus the dead `FUN_0010b920` (`EAX`). None is called from outside the
layer, so a whole-layer port needs no register adaptors at all.

---------------------------------------------------------------------------------------------------------------

## 5. What it calls underneath

| address | called as | now | notes for a port |
|---|---|---|---|
| `0x0010f76c` | `CreateFileA` | `Xbox_CreateFileA` (`src/driving/platform/XboxXapi.cpp`) | resolves `D:\...` with `Xbox_ResolvePath` (`src/common/xboxPath.cpp`); a share mode of 0 becomes read/write share |
| `0x0010f2c3` / `0x0010f3b0` | `ReadFile` / `WriteFile` | Win32 | |
| `0x0010f50a` | `SetFilePointer(h, pos, 0, FILE_BEGIN)` | Win32 | its return value becomes the slot position |
| `0x0010f731` | `GetFileSize(h, 0)` | Win32 | |
| `0x0010e9b9` | `CloseHandle` | Win32 | |
| `0x0010fd88` | `DeleteFileA` | `Xbox_DeleteFileA` | |
| `0x0010e864`, `0x0010e8c5`, `0x0010e8e5`, `0x0010e999` | `CreateEventA`, `SetEvent`, `ResetEvent`, `WaitForSingleObject` | Win32 | |
| `0x0010e9ab` | `Sleep(INFINITE)` | Win32 | only on an unhandled read error |
| `0x0010f8f7` / `0x0010f91f` | `GetLastError` / `SetLastError` | Win32 (`src/driving/platform/XboxStartup.cpp`) | the same thread-local value the Win32 file calls set |
| `0x0010a6e0` | `SIGNAL_create` | `RealSystem.cpp` | auto-reset event at +4 |
| `0x0010a700` / `0x0010a710` | `SIGNAL_post` / `SIGNAL_wait` | **original** | 4.12 |
| `0x0010a9c0` | `THREAD_createparam(thread, fn, param, 0, stack 0, priority 1)` | `RealSystem.cpp` | waits until the thread has started |
| `0x0010a7f0` / `0x0010a7e0` | `THREAD_iscurrent(0)` (main thread?) / `THREAD_yield(ms)` | `RealSystem.cpp` | |
| `0x0010ac40` | `SYNCTASK_run(0)` | `RealSystem.cpp` | run by `waitop` on the main thread |
| `0x0014a4f0`, `0x0014a510`, `0x0014a520`, `0x0014a530` | `MUTEX_create`, `REALMUTEX_destroy`, `MUTEX_lock`, `MUTEX_unlock` | `RealSystem.cpp` | through the kernel's critical-section imports |
| `0x0010a5b0`, `0x0010ad20` | `MEM_copy`, `MEM_fill` | `RealPrint.cpp` | |
| `0x00133a30`, `0x00133d60`, `0x00132767`, `0x001327f9` | `strchr`, `strncpy`, `sprintf`, `toupper` | C runtime | |

Behaviour that matters:

- **Errors.** Status -2 comes from `GetLastError()` read right after the operation. `FILEDEV_read`/`_write`
  clear it first, so a successful read reports 0 whatever happened before; a port that uses another I/O API must
  still produce "0 on success, non-zero on failure" in `op+0x14`, and status -2 on failure. Nothing outside the
  layer reads the error value (Xbox has no `FILESYS_operror`).
- **Loose file first, then archives.** A plain read-mode open goes to disk, and only if `CreateFileA` fails to
  the big files - so `src/loader/file.cpp`'s comment applies here too: every archived file is first reported
  missing by the path resolver. `UFileLoader::FileLoad` (ours) instead tries `"|"+path` first (archives only, via
  `FILE_loadpackz`) and falls back to `FILE_load(path)` (disk, then archives again).
- **Read failures** call the handler at `0x001d1c14` (`0x0005c960`, the disc-error screen, which returns 1) and
  otherwise hang the worker; on a PC a read failure is better reported as an error.
- **Exists counts archived files**: `FILESYS_exists` opens with big-file fallback, so `FILE_exists("data\\...")`
  is true for a file only in an archive.

---------------------------------------------------------------------------------------------------------------

## 6. Who calls the layer

| caller | call | arguments |
|---|---|---|
| `Bond_StartUpSystem` (`0x00059f30`..) | `FILESYS_setmemcallbacks` | `(0x00114630, 0x00114670)` |
| | `FILESYS_init` | `(0x10 files, 0x32 (unused), 0x20 ops)` |
| | `ASYNCFILE_init` | `(0x14, 0)` |
| | `FILESYS_existssync`, then `FILESYS_addbigsync` | `("driving\\misc.viv", 100)`; `("driving\\misc.viv", 0, 100, &0x001e4658)`; success to `0x001e476e` |
| `Bond_CleanUp` (`0x0005a15d`) | `FILESYS_delbigsync`, `ASYNCFILE_restore` | `(*0x001e4658, 100)` if `0x001e476e` |
| `UFileLoader::StartUsingBigFile` (`0x00116ea0`) | `FILE_exists`, `FILESYS_addbigsync` | `(path, 0x100, 2, &0x002434dc)` - the mission archive, e.g. `driving\mis01.viv` |
| `UFileLoader::StopUsingBigFile` (`0x00116ee8`) | `FILESYS_delbigsync` | `(*0x002434dc, 2)` |
| `UFileLoader::AttemptBigFileExists` / `FileExists` | `FILE_exists` | `("|" + path)` / path |
| `UFileLoader::AttemptBigFileSize` / `FileSize` | `FILE_unpacksizez`, then `FILE_sizez` | path (with and without `"|"`) |
| `UFileLoader::FileLoadAt` (`0x00117000`) | `FILE_loadat` | `(path, buffer, size)` |
| `UFileLoader::AttemptBigFileLoad` (ours) | `FILE_loadpackz` | `("|" + path, flags)` |
| `UFileLoader::FileLoadDirectFromDisk` (ours) | `FILE_load` / `FILE_loadz` | `(path, flags)` |
| `FUN_0014a370` (sound bank loader) | `FILE_loadpackz` | `(name, flags)` |
| `AStreamPriv::AStreamPriv` / `~AStreamPriv` | `existssync` + `addbigsync` / `delbigsync` | `(name, 100)`, `(name, flags, 100, &this->bigId (+0x58))` / `(this->bigId, 100)` |
| `DTuningFile::DTuningFile` | `FILESYS_existssync` | `(sprintf'd tuning path, 100)` twice |
| `GameLoop_StartUp` (`0x0005b43e`) | `FILESYS_existssync` | `("driving\\<name>.spe", 100)` |
| `WTriggerManager::Restart` | `FILE_exists` | two paths |
| `TextFile::~TextFile` | `FILESYS_closesync` | |
| `FileNameList::Dump` | `opensync` (mode 6), `writesync(slot, offset, buf, len, 100)`, `closesync` | debug dump of requested files |
| `RRenderer::DoScreenCapture` | `FILE_save` | `(path, buffer, size)` |
| `FUN_0005c9a0` | `FUN_0010d840` | `(0x0005c960)` |
| STREAM (`platform.movie`, for `SNDSTRM`) | open/read/close/completeop/callbackop/priorityop/closesync | 4.11 |

So during a normal driving run the layer sees: `misc.viv` registered at start-up, the mission's `.viv`
registered by `StartUsingBigFile`, hundreds of `FILE_loadpackz("|...")` / `FILE_load` calls during loading
(sync, inside `FILESYS_atomic`), `FILE_exists`/`FILE_sizez` queries, `AStreamPriv`'s audio archive, and the sound
streamer's continuous asynchronous reads at its greedy/normal priorities.

---------------------------------------------------------------------------------------------------------------

## 7. Bugs and quirks a port must decide on

Each is in the original. "Keep" means a faithful port should reproduce it because something can observe it; the
others are latent and can be fixed (say so in the port's comments).

1. **No free-op check** (`FUN_0010cbf0`): with 32 records and the sound streamer, `FILE_*`'s sync ops and ASYNCFILE
   all drawing on them, running out crashes. Fix: fail the submit (return 0), which every caller handles.
2. **`FILESYS_size` clears `w->current`** of worker 0 while the worker may be executing another op. A concurrent
   `waitop` on that op then finds it nowhere, returns -3 early, and the sync caller (`opensync`, a chunked read)
   proceeds before the op is done. Fix: don't touch `current`.
3. **Subfile seek test** compares `base - pos` with the requested offset; it should be `pos - base`. Since a subfile
   shares its archive's HANDLE with every other open subfile, always seeking is what keeps reads correct; the
   comparison only skips the seek when `offset == 0 && pos == base`. A port should always seek for subfiles (and
   may keep the skip for loose files).
4. **The done queue's MUTEX is a byte copy** of the pending queue's initialised critical section
   (`FUN_0014a640`). On the Xbox the copy carries the original's self-referential wait list; under the Win32
   loader it shares the debug info. It is only ever taken uncontended in practice. Fix: give it its own MUTEX.
5. **`FUN_0010cae0` waits on worker 0's start event** whatever worker it starts. Harmless with one worker.
6. **Lost wake-ups in `waitop`** off the main thread: several threads waiting on the same manual-reset event
   reset it for each other; a waiter can then sleep until the next op finishes. The main thread polls and is
   immune. Keep the polling on the main thread (it also runs `SYNCTASK_run`, which the game depends on during
   loads); a port may give other threads a per-op wait.
7. **`completeop` with an unknown handle** dereferences null. Keep the contract (callers never do it), assert in
   debug.
8. **Failed addbig leaks the directory buffer** (`completeop` frees the record, not `big->dir`), and the worker
   does not check `FILE_malloc`, the second read, or an unknown magic (`BIG_dirsize` 0 gives a negative read).
9. **Unbounded copies**: `strcpy` of the archive name into 0x100 bytes, `strncpy` of the part before `'|'` into
   256 bytes, `sprintf` into 256 bytes.
10. **Open with no free slot** fails with error 0, so the op's status is -2 but `GetLastError` says success.
11. **`FILESYS_init` returns `AL = 0` when already initialised** but `FILESYS_initadr` returns 1 in that case.
12. **Ordering**: the pending queue is sorted by `prio << 24 | seq`; after the 24-bit sequence wraps, newer ops sort
    before older ones of the same priority. An op goes before queued ops of equal key (never equal in practice).
    Big files are searched in registration order, so `misc.viv` wins over the mission archive for a name in both.
13. **`delbig` on the Xbox removes the archive from the search list on the worker** (type 10), but frees it only
    in `completeop`; a concurrent open that already matched it keeps a subfile slot sharing a HANDLE that is about
    to be closed.

---------------------------------------------------------------------------------------------------------------

## 8. Suggested port structure and test plan

### 8.1 Structure

One unit, `src/driving/platform/FileSys.cpp` (+ `FileSys.h`), following `RealSystem.cpp`'s style (each function
"the original at the same address", `// AUTOINJECT`). Keep game logic out of it (test harnesses in their own
files, `src/driving/devtools/`). Suggested internal pieces, in the file's order:

1. **Records as structs** with `static_assert`ed offsets: `LockQueue` (0x38), `FsWorker` (0xb8), `FsOp` (0x30),
   `BigFile` (0x118), `FileSlot` (0x10), `ChunkCtx`, `FileReq`, `ResolveCtx`; reuse `RealMutex`, `RealSignal`,
   `RealThread` from `RealSystem.h`.
2. **Globals by address** (`#define FsOperations (*(FsWorker **)0x00242870u)` etc., as RealSystem.cpp does), so the
   state stays where the rest of the binary and the dumps expect it during the transition, and `global_clears.py`
   can later own them.
3. **Queue helpers** as plain C++ functions (no need to patch them: no outside callers), `Q_unlink` taking
   `(q, node)` normally. Keep `FUN_0014a540` the original.
4. **File device** (slot table, `FILEDEV_*`, `BIG_*`, the resolver) - pure functions over the slot table and a
   directory buffer, easy to unit-test.
5. **Worker, submission, status/wait/complete/callback/cancel/priority, atomic.**
6. **Sync wrappers and FILE_*.** `FUN_0010b5f0` becomes an ordinary function; `FILESYS_chunkdone` an ordinary
   callback.
7. **ASYNCFILE**: `init`, `restore`, `cancel`, `free` (the rest is dead - leave it unported, or port it for
   completeness without patching).
8. **Patches**: `AUTOINJECT` the entry points of 4.13 (cdecl, so `XBE_ABI_CHECK` covers them); the AUTOGEN
   declarations in `src/driving/platform/FILE.cpp` become the real definitions. `FILESYS_write` (`0x0010cf20`),
   `0x0010da00`, `0x0010b550`, `0x0010bf60`, `0x0010c050` are not Ghidra functions; give them `FUNC_AT` only if
   something must reach them by address (nothing outside does).

Port order, each step buildable and testable:

1. Queue + file device + big-file directory (no threads): shadow-testable against the originals.
2. Worker, submission and the async API, plus the sync wrappers and `FILESYS_atomic`; patch all FILESYS entry
   points at once (they share the worker state and the register-argument internals).
3. FILE_* helpers (then `UFileLoader`'s AUTOGEN calls go away).
4. ASYNCFILE `init`/`restore`.
5. Later, with the STREAM port: the three STREAM helpers.

Steps 2-4 can be one commit; step 1 can land first only if its functions are not patched yet (they have no
outside callers, so they can be compiled and tested without being injected).

### 8.2 Tests

Tests run only on the remote runner (`tools/runner/remote.sh`, docs/test-runner.md); nothing here needs the game
run locally.

**Shadow tests** (a `src/driving/devtools/FileSysShadow.cpp`, in its own unit, run at start-up behind a switch,
like the action engine's `*Shadow.cpp`):

- `BIG_kind`, `BIG_dirsize`, `BIG_find` against the originals on the real directories of every
  `disc/driving/*.viv` (read the first `BE32(+0xc)` bytes): look up every entry by name (with case changes and
  with `/` vs `\`) and a few missing names, compare pointer offset, offset and size. Call the originals' register
  forms through small naked thunks in the test file, or compare only `BIG_find` (`0x0010de40`, cdecl), which
  covers the others.
- The queue: run the same random sequence of pushes, pops, sorted inserts, removes and finds on two queues, one
  through the originals (`0x0014a5f0`.. are cdecl except `FUN_0014a550`) and one through the port; compare
  count/head/tail/link order and the flag bit after each step.
- `FILEDEV_read` clamping: open a loose file and a subfile through both, read at offsets -1, 0, mid, size-1, size,
  size+10 with counts that cross the end; compare bytes and the slot's `pos`.

**In-game runs** (each names what it covers):

- Boot and load a mission - start-up `existssync`/`addbigsync` of `misc.viv`, `StartUsingBigFile` of the mission
  archive, hundreds of `FILE_loadpackz("|...")`/`FILE_load`, `FILE_sizez`, `FILE_unpacksizez`, `FILE_loadat`:

  ```
  tools/runner/remote.sh drive fs_mission2 "-Exe 'Release\driving.exe' -GameArgs '-mission 2' -DumpAfterMs 8000 -StopPattern 'dumping frame'"
  ```

  Compare the dumped frame with a baseline from before the port, and the log's `-------- Loading file` lines
  (UFileLoader prints each) - the same files, in the same order, none missing.
- A second mission with a different archive (e.g. `-mission 1` or a `mis13*` one) - delbig of the first archive
  is covered by `StopUsingBigFile` only when a mission ends, so also run one through to the menu or a restart if
  a harness step exists (`WTriggerManager::Restart` also calls `FILE_exists`).
- Sound streaming (STREAM -> `FILESYS_open/read/priorityop/close`, the asynchronous path and the callbacks on
  the worker thread): let the level play for a minute with music, `-GameHold accelerate`, and check that music
  and speech keep playing without gaps (listen to a capture, or log the streamer's buffered byte count); this is the test that exercises concurrency (sync loads at
  priority 100/99 against the streamer's ops).
- `FILE_save` via a screenshot capture, if the harness can trigger `RRenderer::DoScreenCapture`; otherwise a
  shadow call that saves and reloads a small buffer under `D:\` resolved to a scratch folder.
- Shutdown: a clean exit runs `Bond_CleanUp` (`delbigsync(misc.viv)`, `ASYNCFILE_restore`).

Add a debug-only log of every op (type, handle, priority, path, status, bytes) behind a switch; the same switch
on a pre-port build gives a trace to diff against (op order may differ under concurrency; per-path results and
byte counts must not). Per the GameCube checks note (`docs/gamecube-checks.md`), add `NF_WARN`/`NF_ASSERT` for
the latent bugs of section 7 (empty free list, unknown handle in `completeop`, name overflow).
