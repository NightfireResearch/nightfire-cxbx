// The entries through which the original's code calls the functions in game/obj/Sensor.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/Sensor.h"

// AUTOLTCG
void __declspec(naked) Asm_Sensor_SetBeam(void) {
    // Assembly wrapper for custom calling convention
    // Original game code calls with: param_1 (targetObj) on stack, param_2 (sensorObj) in EDI, param_3 (sensor) in ESI
    _asm {
        push esi                    // Push param_3 (sensor) from ESI
        push edi                    // Push param_2 (sensorObj) from EDI
        push dword ptr [esp + 12]   // Push param_1 (targetObj) from stack
        call Sensor_SetBeam
        add esp, 12                 // Clean up stack (3 params)
        ret
    }
}
