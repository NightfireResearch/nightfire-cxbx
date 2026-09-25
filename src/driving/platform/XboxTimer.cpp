#include "XboxTimer.h"
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The tick source, which standalone becomes ours - docs/driving-engine-plan.md section 2.
//
// Timer_Init (0x0010ae50) asks for a periodic callback at the video refresh rate:
//
//     SysTimerHandle = timeSetEvent(1000 / freqHz, 0, TIMER_ontick, 0, TIME_PERIODIC);
//
// and TIMER_ontick (0x0010ae10) runs the task list that includes RealClock_InterruptHandler, which is what
// advances the game's Clock. So this one call is where the whole simulation's sense of time comes from.
//
// On the console that timeSetEvent is XAPI's own, statically linked into the XBE, and it is a small operating
// system in itself: it starts a thread (FUN_0010ece5) that initialises sixty-four kernel timer objects, waits
// on all of them with KeWaitForMultipleObjects, and dispatches callbacks as each expires - through
// KeInitializeTimerEx, KeSetTimerEx, KeQueryInterruptTime, KeSetEvent, NtSetEvent and NtPulseEvent. Under the
// standalone loader every one of those is an unimplemented kernel import, and implementing them would mean
// reproducing the Xbox's dispatcher objects and DPC semantics so that a thread we control can wait on them -
// to deliver a callback that Windows will deliver by itself.
//
// So the layer is replaced rather than the kernel underneath it. Windows has this exact API: XAPI's
// multimedia timer is Windows' multimedia timer, argument for argument and flag for flag, because that is
// where it was ported from. TIMER_ontick even ends in "ret 0x14" - five stack arguments, the shape of an
// LPTIMECALLBACK.
//
// What this changes about the plan's section 2: the jitter it blames on CXBX's timeSetEvent emulation is now
// winmm's, with nothing in between, and timeBeginPeriod(1) is what makes that a millisecond rather than the
// scheduler's 15.6 ms quantum. Whether the game's original Scheduler::Run semantics behave with a tick source
// this even is step 4 of the plan's order of work, and is a measurement to make once the engine runs - not a
// reason to change anything here.
//
// The one caller is Timer_Init, and nothing kills the timer afterwards (Timer_Active is set once and never
// cleared), so there is no timeKillEvent to match.
// ---------------------------------------------------------------------------------------------------------------

// Set once, so that the process-wide period is not raised repeatedly if the game ever re-arms the timer.
static bool g_periodRaised = false;

unsigned __stdcall Xbox_timeSetEvent(unsigned delayMs, unsigned resolutionMs,
                                     LPTIMECALLBACK callback, DWORD_PTR user, unsigned flags) {
    // The game asks for resolution 0, meaning "whatever you have". What Windows has by default is the
    // scheduler's 15.6 ms quantum, which at a 20 ms period would deliver ticks in visible pairs; asking for
    // 1 ms costs a little power and is what every game of this era did.
    if (!g_periodRaised) {
        if (timeBeginPeriod(1) == TIMERR_NOERROR)
            g_periodRaised = true;
        else
            printf("[timer] timeBeginPeriod(1) refused - ticks will be as coarse as the scheduler's quantum\n");
    }

    unsigned resolution = resolutionMs != 0 ? resolutionMs : 1;
    unsigned id = timeSetEvent(delayMs, resolution, callback, user, flags);
    if (id == 0) {
        printf("[timer] timeSetEvent(%u ms) failed - the game's clock will never advance\n", delayMs);
        fflush(stdout);
        return 0;
    }

    printf("[timer] tick every %u ms (timer %u), callback at 0x%08x\n",
           delayMs, id, (unsigned)(uintptr_t)callback);
    fflush(stdout);
    return id;
}

// ---------------------------------------------------------------------------------------------------------------
// The cycle counter - docs/driving-engine-plan.md section 2.1.
//
// An Xbox title may assume RDTSC counts at 733,333,333 Hz, the console's CPU clock, and this one does. CXBX
// rewrote every RDTSC to count at that rate; standalone the instruction runs natively and counts at whatever
// the host runs at. The audit of every RDTSC in the image (all eleven instructions, plus two byte matches that
// are data) found that nothing the player sees depends on it:
//
//   0x0008bfdc  RRenderHigh::Render    frame rate as 733e6 / cycles per frame, smoothed into fgThis + 0x40.
//                                      Its one reader is SMissionManager::Update (0x000b8757), which keeps
//                                      per-section minimum, maximum and running total of the frame rate -
//                                      statistics, nothing that steers the game. Corrected below anyway.
//   0x000f4ed2, 0x000f4efd, 0x000f4f3d, 0x000f4f75, 0x000f52ea, 0x000f52f4, 0x000f54c1, 0x000f54e5
//                                      EAGL's profiler: timers that start, stop and accumulate cycles into
//                                      node + 0x110, and a frame boundary (0x000f52d0, from EAGL's EndFrame)
//                                      that keeps a 32-frame history. Only ever compared with each other, so
//                                      the rate cancels out. Left alone.
//   0x000f4520                         a bare "rdtsc; ret" in EAGL with no static caller - render methods are
//                                      compiled at run time, so that proves nothing. Redirected below, so
//                                      that anything that turns up calling it gets the console's rate.
//   0x0014bee4  XAPILIB::QueryPerformanceCounter, paired with QueryPerformanceFrequency (0x0014bef1)
//                                      returning the literal 733,333,333. Their only caller in the image is
//                                      D3D8's screen-capture recorder (FUN_00171d40), behind the graphics
//                                      seam and never reached. Replaced below with the host's pair anyway,
//                                      as the action engine's are, for anything reached from generated code.
//   0x0016ec30                         a bare RDTSC inside D3D8, used by the nv2a driver's vblank prediction
//                                      (FUN_0016ee30, FUN_0016f0f0). That driver does not run behind the
//                                      seam. Left alone.
//
// The plan used to list "the clock runs fast" here, on the evidence of the game's own log timestamps. Those
// are GLoadingScreen::Status (0x000e2ff0) printing TIMER_gettick - the 50 Hz tick, not the cycle counter -
// through "(%02d:%02d:%02d)" with tick / 3600, (tick % 3600) / 60 and (tick % 60) / 6: minutes, seconds and
// tenths at 60 Hz, which read as hours, minutes and seconds look several times too fast. They are right.
//
// QueryPerformanceCounter rather than RDTSC scaled by a measured host frequency, for the reason the action
// engine gives (src/action/game.cpp, timestamp()): it is the fixed-rate counter that measurement would be
// approximating, and it does not drift when the CPU changes speed.
// ---------------------------------------------------------------------------------------------------------------

static const unsigned long long XBOX_CPU_HZ = 733333333ull;

// The host counter converted to the console's cycle rate. Split into whole seconds and a remainder so that
// the multiply cannot overflow: counter * 733e6 would overflow 64 bits within an hour of uptime at 10 MHz.
static unsigned long long __cdecl Xbox_ReadCycleCounter(void) {
    static LARGE_INTEGER frequency = { 0 };
    if (frequency.QuadPart == 0) {
        LARGE_INTEGER f;
        if (!QueryPerformanceFrequency(&f) || f.QuadPart == 0)
            f.QuadPart = 10000000;   // being wrong beats a divide by zero
        frequency = f;
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    unsigned long long ticks = (unsigned long long)now.QuadPart;
    unsigned long long hz = (unsigned long long)frequency.QuadPart;
    return (ticks / hz) * XBOX_CPU_HZ + ((ticks % hz) * XBOX_CPU_HZ) / hz;
}

// RRenderHigh::Render, 0x0008bfdc:
//
//     0f 31           rdtsc
//     8b 4c 24 04     mov ecx, [esp + 4]
//
// Six bytes, overwritten with a call here and a nop, so the call has to do the MOV as well - one slot further
// up the stack, because of the return address. The C function clobbers only EAX, ECX and EDX, and the next
// instruction sets ECX again. The x87 stack is empty here: the site follows a call to GHud::Render.
static void __declspec(naked) Xbox_RenderFrameRateCounter(void) {
    __asm {
        call Xbox_ReadCycleCounter
        mov  ecx, [esp + 8]
        ret
    }
}

// 0x000f4520, "rdtsc; ret". RDTSC leaves ECX alone and a caller of an assembly helper may rely on that.
static void __declspec(naked) Xbox_RdtscHelper(void) {
    __asm {
        push ecx
        call Xbox_ReadCycleCounter
        pop  ecx
        ret
    }
}

// XAPILIB::QueryPerformanceCounter and QueryPerformanceFrequency: __stdcall, one pointer, return 1 (each ends
// RET 4). Replaced as a pair, because consistency between the two is all a caller can depend on.
static BOOL __stdcall Xbox_QueryPerformanceCounter(LARGE_INTEGER *counter) {
    QueryPerformanceCounter(counter);
    return TRUE;
}

static BOOL __stdcall Xbox_QueryPerformanceFrequency(LARGE_INTEGER *frequency) {
    if (!QueryPerformanceFrequency(frequency) || frequency->QuadPart == 0)
        frequency->QuadPart = 10000000;
    return TRUE;
}

// Each site is checked against the bytes the audit read before it is written, so that a different build of
// the XBE is reported rather than patched in the middle of an instruction.
static bool PatchSite(unsigned address, const unsigned char *expected, size_t length,
                      unsigned char opcode, const void *target) {
    unsigned char *site = (unsigned char *)address;
    if (memcmp(site, expected, length) != 0) {
        printf("[timer] 0x%08x is not the expected RDTSC site - not patching it\n", address);
        return false;
    }
    DWORD previous = 0;
    if (!VirtualProtect(site, length, PAGE_EXECUTE_READWRITE, &previous)) {
        printf("[timer] could not make 0x%08x writable (error %lu)\n", address, GetLastError());
        return false;
    }
    site[0] = opcode;                                           // call or jmp rel32
    *(int *)(site + 1) = (int)((const unsigned char *)target - (site + 5));
    memset(site + 5, 0x90, length - 5);                         // nop the rest
    return true;
}

void Inject_XboxCycleCounter(void) {
    static const unsigned char RENDER_FPS[]  = { 0x0f, 0x31, 0x8b, 0x4c, 0x24, 0x04 };
    static const unsigned char RDTSC_RET[]   = { 0x0f, 0x31, 0xc3, 0x90, 0x90 };
    static const unsigned char QPC[]         = { 0x8b, 0x4c, 0x24, 0x04, 0x0f, 0x31 };
    static const unsigned char QPF[]         = { 0x8b, 0x44, 0x24, 0x04, 0x83, 0x60, 0x04, 0x00 };

    PatchSite(0x0008bfdc, RENDER_FPS, sizeof(RENDER_FPS), 0xE8, (const void *)Xbox_RenderFrameRateCounter);
    PatchSite(0x000f4520, RDTSC_RET, sizeof(RDTSC_RET), 0xE9, (const void *)Xbox_RdtscHelper);
    PatchSite(0x0014bee0, QPC, sizeof(QPC), 0xE9, (const void *)Xbox_QueryPerformanceCounter);
    PatchSite(0x0014bef1, QPF, sizeof(QPF), 0xE9, (const void *)Xbox_QueryPerformanceFrequency);
}
