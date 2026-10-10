# The driving engine's input layer

The pads to the game's actions, and back to the pads' motors: IOModule's devices and their mappings, the
control configurations, the action queues, and force feedback. Finished on 10 October 2026 (`src/driving/engine/`,
`src/driving/platform/Pad.cpp` and `XboxInput.cpp` below it). Every address is Driving.xbe's.

**Status:** `engine.input` is 100% ours by bytes; what the coverage tool still counts live are five exception
funclets of functions in other subsystems, filed by address. The last pass ported 40 functions - IFeedback (with its
thread function at 0x0004fe10, an entry point Ghidra had not made a function), InputConfigManager, InputTable,
InputToAction, the text helpers (AdvanceFilePtr, getActionID, and the two path builders at 0x00051e90/0x00051f30)
and the rest of ActionQueueManager's vector - on top of IOModule, the devices and the queues ported earlier.
A shadow test compares them with the originals (below); it and the lockstep runs of missions 1-8 are still to
be run.

## Layout

| File | What |
|---|---|
| `engine/IOModule.hpp/.cpp` | IOModule: the devices and their mappings, the sync task that polls the pads and sends the actions |
| `engine/InputDevice.hpp/.cpp` | InputDevice, DeviceScalar and XBoxPadDevice: a device's controls, their values and transitions |
| `engine/InputConfig.h/.cpp` | InputConfigManager (Master.def, the configurations, their front-end labels, SetConfig), InputToAction and InputTable (a device's mappings, from a .def file), AdvanceFilePtr, getActionID, BuildFileName, BuildPath |
| `engine/ActionQueue.hpp/.cpp` | ActionQueue (a consumer's ring of 200 actions), ActionQueueManager (a `std::vector<ActionQueue *>` with its compiled members, which the linker shares with other pointer vectors) |
| `engine/Feedback.h/.cpp` | IFeedback and the four ports' records: jolts, rumbles, vibrations, the thread that runs Update each timer tick |

The control files are in misc.viv's `data\control\`: Master.def lists eight "POV" configurations and one "Driving";
each .def is a header line, mappings (action, control, update method letter, threshold) up to a FRONTEND line, then
one front-end label (a locale string id) per slot name up to END. FrontEnd.def holds the menus' mappings and has no
FRONTEND part. The launch page carries the choices (`LaunchPage::drivingConfig`, `povConfig`, `invertedControls`,
`vibration`, `controllerPort` in engine/GameLoop.h); `controllerPort` is the port IOModule gives a device (Ghidra's
`glbIFeedback`).

The overlay layouts the generator used to make for InputToAction, InputTable, InputTableEntry and
ActionQueueManager are real structs in these headers now.

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_INPUTSHADOW` | first tick | Master.def and 199 rewritten copies; every configuration's .def and mutated copies through ParseDefFileForFrontEnd and LoadParseDefFile (on InputToActions built on the live pad device); InitAndPreload and Shutdown on scratch managers with varied launch-page choices, the live mapping compared; SetConfig over every type and configuration; the getters and GetLocaleID on every label and special id under five track names; InputTable's growth; the text helpers; IFeedback's Update (4000 perturbed states, a recording stand-in for XInputSetState, the feedback thread suspended), constructor, destructor, Pause and setters on edge values; the queue vector's push_back, _Insert_n in all three branches, _Tidy and fill |

Not in the shadow: the timer callback, the thread function and the thread's start and stop (they need the real
signal and thread), and the vector's `_Xlen` (it throws). The game exercises them: vibration on in the launch page
starts the thread with the first IFeedback (main's, then each PBondCar's), and the last destructor stops it at the
end of a mission.

## What the port taught

- **Ghidra's `glbIFeedback` is the launch page's controller port** (0x00243b90 + 0x990), not a constant 0: IOModule
  gives that port its device, and IFeedback's records are indexed by it.
- **The two "strcat"s are path builders.** 0x00051e90 is directory + name + "." + extension (no dot for an empty
  extension) and 0x00051f30 the same with a subdirectory and "/"; both take the buffer in ECX and pop their stack
  arguments, so they are `__fastcall` with EDX unused. Ten files called them by address.
- **The dither table at 0x0018d790 is the byte values with their bits reversed** - an ordered dither, so a pulse
  level of n switches the motors on n ticks in 256, spread evenly.
- **The linker folded identical vector members:** `_Tidy` at 0x0004f400 is also the scheduler's, the collision
  system's and the static's destructor's; the pointer fill at 0x0004f3e0 is called from seven other vectors.

## Odd things in the original, kept

- GetLocaleID swaps some labels for 2721 (0xaa1) by track: 1424 off snow1a_mis3, 2708 on tracks with "uw_" in
  their name, 2712/2713 and 2723 on paris_mis01 outside the driving type (2723 becomes 1709 for POV configurations
  6 and 7), and 2719 everywhere.
- Nothing in the parsers is checked: an unknown control name indexes InputToAction's table -1, an unknown slot name
  writes the configuration's text pointer, a line without a carriage return runs off the text, and a name in
  Master.def before any section goes to configs[2]. ParseMasterConfigFile reads the first line; the .def parsers
  skip it.
- InputConfigManager frees a pointer at +0x2c in Shutdown that nothing sets; the StringToNumber of slot names lives
  only while InitAndPreload reads the labels.
- IFeedback's records keep two float inputs (x100 for the pulse level, x125 for the steady speed) that none of its
  methods write. With vibration off or feedback held off by an event, Update stops the motors and zeroes the three
  durations but leaves the levels; Pause keeps each port's user count.
- The thread is created with a pointer to a static 0x2000-byte block (the PS2's stack), which the Xbox's
  THREAD_create ignores.
