#ifndef DRIVING_DEVTOOLS_INPUTSHADOW_H_
#define DRIVING_DEVTOOLS_INPUTSHADOW_H_

// NIGHTFIRE_INPUTSHADOW=1: the input layer's ports (engine/InputConfig.cpp, engine/Feedback.cpp, the vector in
// engine/ActionQueue.cpp) against the originals, on the disc's control files, mutated copies of them and random
// inputs. See InputShadow.cpp. Run from the first simulation tick, once the configurations are loaded.
void InputShadow_Run(void);

#endif // DRIVING_DEVTOOLS_INPUTSHADOW_H_
