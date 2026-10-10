#include "IOModule.hpp"

#include <string.h>

#include "ActionQueue.hpp"
#include "GameInterfaces.hpp"
#include "InputConfig.h"
#include "InputDevice.hpp"
#include "UMemory.hpp"
#include "../platform/Pad.hpp"

// The IOModule static and its construction guard (bit 0), GetIOModule's function-local static.
#define gIOModule ((IOModule *)0x001e45a0)
#define gIOModuleGuard (*(uint32_t *)0x001e4644)
// The pad port input comes from: the launch page's controllerPort (GameLoop.h). Named glbIFeedback in Ghidra.
#define glbActivePort (*(int *)0x00244520)
// The simulation's step count, and how many steps must have run before the unplugged message is shown (2).
#define SimStepCount (*(int *)0x00234e34)
#define kUnplugMinSimSteps (*(const int *)0x001b7028)

// The game's own names for its allocations, as the original passes them.
#define kXBoxPadDeviceTag ((const char *)0x0018da2c)
#define kInputToActionTag ((const char *)0x0018da1c)

// Called at their addresses, all __cdecl: the timer, the sync-task list and the CRT's atexit. The tasks are
// named by the originals' addresses (patched to jump here), so adding and removing find the same entry
// whichever side registered it.
typedef int (__cdecl *IntFn)();
typedef void (__cdecl *SyncTaskAddFn)(void *task, int interval, int delay);
typedef void (__cdecl *SyncTaskDelFn)(void *task);
typedef int (__cdecl *AtexitFn)(void (__cdecl *fn)());
#define TIMER_gettick ((IntFn)0x0010a6c0)
#define TIMER_getfrequency ((IntFn)0x0010a6d0)
#define SYNCTASK_add ((SyncTaskAddFn)0x0010aa50)
#define SYNCTASK_del ((SyncTaskDelFn)0x0010abc0)
#define CRT_atexit ((AtexitFn)0x00132a7b)
#define kUpdateTask ((void *)0x000516d0)
#define kPAD_updateTask ((void *)0x00108490)
#define kIOModuleAtexit ((void (__cdecl *)())0x0015ce00)   // IOModule's trivial destructor: a bare RET

static const int kPorts = 4;
static const int kInputSync = 0x73;   // ACTION_INPUTSYNC, sent after every device's batch

// AUTOINJECT
IOModule* IOModule::GetIOModule() {
    if (!(gIOModuleGuard & 1)) {
        gIOModuleGuard |= 1;
        IOModule *io = gIOModule;
        io->updatingEnabled = false;
        io->numControllers = 0;
        io->padStable = false;
        io->checkUnpluggedCount = 0;
        io->unpluggedMessageShown = false;
        io->controllerUnplugged = false;
        io->currentTick = 0;
        io->unused98 = 0;
        CRT_atexit(kIOModuleAtexit);
    }
    return gIOModule;
}

// AUTOINJECT
void IOModule::Initialize() {
    // PAD_init adds PAD_update as a sync task of its own; from here on Update does the polling instead.
    PAD_init();
    SYNCTASK_del(kPAD_updateTask);
    resendAll = false;
    resyncDevices = false;
    repeatDelayTicks = TIMER_getfrequency() * 300 / 1000;      // 300 ms
    repeatIntervalTicks = TIMER_getfrequency() * 150 / 1000;   // 150 ms
    CreateDevices();
}

// A device and its mappings for the active port, nothing for the others. As in the original, the slots are
// indexed by numControllers rather than by port (so only a first call after ReleaseDevices fills 0..3), and a
// failed allocation is not checked before Initialize.
// AUTOINJECT
void IOModule::CreateDevices() {
    int port = glbActivePort;
    for (int i = 0; i < kPorts; i++) {
        XBoxPadDevice *device = nullptr;
        if (i == port) {
            void *memory = UMemory::FastAlloc(0x56c, kXBoxPadDeviceTag);
            device = memory ? ((XBoxPadDevice *)memory)->Construct(i) : nullptr;
            ((InputDevice *)device)->Initialize();
        }
        padDevices[numControllers] = device;
        InputToAction *mapping = nullptr;
        if (i == port) {
            void *memory = UMemory::FastAlloc(0x10, kInputToActionTag);
            mapping = memory ? ((InputToAction *)memory)->Construct(device) : nullptr;
        }
        inputActionMappings[numControllers] = mapping;
        numControllers++;
    }
}

// AUTOINJECT
int IOModule::Update() {
    IOModule *io = GetIOModule();
    if (io->updatingEnabled) {
        PAD_update();
        io->currentTick = TIMER_gettick();
        io->CheckUnplugged();
        if (!io->controllerUnplugged)
            io->UpdateAllDevices();
    }
    return 0;
}

// Input stops at once when the active pad is pulled out; the HUD's message (which pauses) waits until there is a
// HUD and the simulation has run a couple of steps. Nothing is released on unplugging or resynchronised on
// plugging back in, so the first update after compares against the state from before.
// AUTOINJECT
void IOModule::CheckUnplugged() {
    checkUnpluggedCount++;
    padStable = true;
    bool connected = PAD_getpadtype(glbActivePort) != 0;
    if (!padStable)   // the PS2's debounce, always passed on Xbox
        return;
    bool shown = unpluggedMessageShown;
    controllerUnplugged = !connected;
    if (connected) {
        if (shown) {
            GHud *hud = GHud::TheApp();
            if (hud != nullptr) {
                hud->SetControllerUnplugged(false);
                unpluggedMessageShown = false;
            }
        }
    } else if (!shown) {
        GHud *hud = GHud::TheApp();
        if (hud != nullptr && SimStepCount >= kUnplugMinSimSteps) {
            ActionQueueManager::GetActionQueueManager()->FlushAllQueues();
            hud->SetControllerUnplugged(true);
            unpluggedMessageShown = true;
        }
    }
}

// resyncDevices (set by the movie players) polls every device twice, so this update sees no transitions - the
// button that skipped a movie does not also act in the game.
// AUTOINJECT
void IOModule::UpdateAllDevices() {
    if (resyncDevices) {
        for (int i = 0; i < numControllers; i++)
            if (padDevices[i] != nullptr)
                ((InputDevice *)padDevices[i])->PollDevice();
    }
    resyncDevices = false;
    for (int i = 0; i < numControllers; i++) {
        XBoxPadDevice *device = padDevices[i];
        if (device != nullptr) {
            ((InputDevice *)device)->PollDevice();
            SendGameMessages(device, inputActionMappings[i]);
        }
    }
    resendAll = false;
}

// For every control, every mapping entry whose condition holds sends its action, with the control's current
// value (copied bit for bit, once per control). An entry is only looked at when the control's value changed,
// when resendAll is set, or for the auto-repeat methods, which keep their state - the tick of the last send and
// 1 after a transition, 2 after a repeat, 3 blocked - as ints in the control's two extra slots, shared by all its
// entries. The batch ends with ACTION_INPUTSYNC.
// AUTOINJECT
void IOModule::SendGameMessages(InputDevice *device, InputToAction *mapping) {
    int scalarCount = device->GetNumDeviceScalar();
    ActionData message;
    message.action = 0;
    message.source = 0;
    message.value = 0.0f;
    for (int i = 0; i < scalarCount; i++) {
        DeviceScalar *scalar = &device->scalars[i];
        InputTable *table = &mapping->tables[i];
        int entryCount = table->count;
        uint32_t currentBits;
        memcpy(&currentBits, scalar->current, sizeof(currentBits));
        bool changed = !(*scalar->current == *scalar->previous);   // NaN counts as a change, as in the original
        for (int j = 0; j < entryCount; j++) {
            InputTableEntry *entry = &table->entries[j];
            int method = entry->updateMethod;
            if (!resendAll && method != UPDATE_REPEAT_DOWN && method != UPDATE_REPEAT_UP && !changed)
                continue;
            message.action = 0;
            bool send = false;
            switch ((uint32_t)method) {
            case UPDATE_ON_CHANGE:
                send = true;
                break;
            case UPDATE_DOWN:
                send = scalar->isDownTransition();
                break;
            case UPDATE_UP:
                send = scalar->isUpTransition();
                break;
            case UPDATE_ABOVE:
                send = scalar->isDownTransitionThreshold(entry->threshold);
                break;
            case UPDATE_BELOW:
                send = scalar->isUpTransitionThreshold(entry->threshold);
                break;
            case UPDATE_CENTRED:
                send = scalar->isCenteredTransition(0.2f);
                break;
            case UPDATE_IF_INVERTED:
                send = InputConfigManager::Get()->IsInverted();
                break;
            case UPDATE_NOT_INVERTED:
                send = !InputConfigManager::Get()->IsInverted();
                break;
            case UPDATE_REPEAT_DOWN:
            case UPDATE_REPEAT_UP: {
                int *lastTick = (int *)scalar->extra1;
                int *state = (int *)scalar->extra2;
                bool down = method == UPDATE_REPEAT_DOWN;
                if (down ? scalar->isDownTransition() : scalar->isUpTransition()) {
                    *lastTick = currentTick;
                    *state = 1;
                    send = true;
                } else if ((down ? scalar->isDown() : scalar->isUp()) && *state != 3) {
                    // The first repeat waits the longer delay; a control that never fired waits the interval.
                    int wait = *state == 1 ? repeatDelayTicks : repeatIntervalTicks;
                    if (currentTick > *lastTick + wait) {
                        *lastTick = currentTick;
                        *state = 2;
                        send = true;
                    }
                }
                break;
            }
            default:
                continue;
            }
            if (send) {
                message.action = entry->action;
                message.source = device->port;
                memcpy(&message.value, &currentBits, sizeof(currentBits));
            }
            if (message.action != 0)
                ActionQueueManager::GetActionQueueManager()->FeedQueue(message);
        }
    }
    message.action = kInputSync;
    message.source = device->port;
    message.value = 0.0f;
    ActionQueueManager::GetActionQueueManager()->FeedQueue(message);
}

// Only the release entries (- and <) fire, with the value 0. The zero written to each control's current value
// stays until the next poll, so anything still held gives a fresh down transition then, under whatever
// configuration the caller has switched to.
// AUTOINJECT
void IOModule::ReleaseAllButtons() {
    InputToAction *mapping = inputActionMappings[glbActivePort];
    XBoxPadDevice *device = padDevices[glbActivePort];
    int scalarCount = ((InputDevice *)device)->GetNumDeviceScalar();
    ActionData message;
    message.action = 0;
    message.source = 0;
    message.value = 0.0f;
    for (int i = 0; i < scalarCount; i++) {
        DeviceScalar *scalar = &device->scalars[i];
        memset(scalar->current, 0, sizeof(float));
        InputTable *table = &mapping->tables[i];
        int entryCount = table->count;
        for (int j = 0; j < entryCount; j++) {
            message.action = 0;
            InputTableEntry *entry = &table->entries[j];
            bool send;
            if (entry->updateMethod == UPDATE_UP)
                send = scalar->isUpTransition();
            else if (entry->updateMethod == UPDATE_BELOW)
                send = scalar->isUpTransitionThreshold(entry->threshold);
            else
                continue;
            if (send) {
                message.action = entry->action;
                message.value = 0.0f;
                message.source = device->port;
            }
            if (message.action != 0)
                ActionQueueManager::GetActionQueueManager()->FeedQueue(message);
        }
    }
}

// AUTOINJECT
bool IOModule::EnableUpdating(bool enable) {
    if (updatingEnabled != enable) {
        if (enable)
            SYNCTASK_add(kUpdateTask, 2, 0);
        else
            SYNCTASK_del(kUpdateTask);
    }
    updatingEnabled = enable;
    return enable;
}

// AUTOINJECT
void IOModule::Release() {
    if (updatingEnabled)
        SYNCTASK_del(kUpdateTask);
    updatingEnabled = false;
    ReleaseDevices();
}

// AUTOINJECT
void IOModule::ReleaseDevices() {
    for (int i = 0; i < numControllers; i++) {
        if (InputToAction *mapping = inputActionMappings[i]) {
            mapping->Destruct();
            UMemory::FastFree(mapping, 0x10);
            inputActionMappings[i] = nullptr;
        }
        if (XBoxPadDevice *device = padDevices[i]) {
            ((InputDevice *)device)->Delete(1);
            padDevices[i] = nullptr;
        }
    }
    numControllers = 0;
}
