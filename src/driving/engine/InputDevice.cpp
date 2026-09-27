#include "InputDevice.hpp"

#include <string.h>

#include "UMemory.hpp"
#include "../platform/Pad.hpp"

#define kInputDeviceVtable ((void *)0x0018d9fc)
#define kXBoxPadDeviceVtable ((void *)0x0018db38)

static const int kPadScalars = 20;

// FUNC_AT(0x00050b80)
DeviceScalar* DeviceScalar::Construct() {
    type = 0;
    name[0] = '\0';
    previous = nullptr;
    current = nullptr;
    extra1 = nullptr;
    extra2 = nullptr;
    return this;
}

// AUTOINJECT
void DeviceScalar::InitializeDeviceScalar(int type_, char *name_, float *previous_, float *current_, float *extra1_,
                                          float *extra2_) {
    type = type_;
    strcpy(name, name_);
    previous = previous_;
    current = current_;
    extra1 = extra1_;
    extra2 = extra2_;
}

// AUTOINJECT
bool DeviceScalar::isDown() {
    if (type == DEVICE_SCALAR_DIGITAL)
        return *current == 1.0f;
    return *current >= 0.5f;
}

// AUTOINJECT
bool DeviceScalar::isUp() {
    if (type == DEVICE_SCALAR_DIGITAL)
        return *current == 0.0f;
    if (type == DEVICE_SCALAR_ANALOG)
        return *current < 0.5f;
    return *current < -0.5f;
}

// AUTOINJECT
bool DeviceScalar::isDownTransition() {
    if (type == DEVICE_SCALAR_DIGITAL)
        return *current == 1.0f && *previous == 0.0f;
    // Axes and analogue buttons alike: the original compares the type with 1 and then ignores the answer.
    return *current >= 0.5f && *previous < 0.5f;
}

// AUTOINJECT
bool DeviceScalar::isUpTransition() {
    if (type == DEVICE_SCALAR_DIGITAL)
        return *current == 0.0f && *previous == 1.0f;
    if (type == DEVICE_SCALAR_ANALOG)
        return *current < 0.5f && *previous >= 0.5f;
    return *current < -0.5f && *previous >= -0.5f;
}

// AUTOINJECT
bool DeviceScalar::isDownTransitionThreshold(float threshold) {
    return *current >= threshold && *previous < threshold;
}

// AUTOINJECT
bool DeviceScalar::isUpTransitionThreshold(float threshold) {
    return *current < threshold && *previous >= threshold;
}

// AUTOINJECT
bool DeviceScalar::isCenteredTransition(float threshold) {
    float low = -threshold;
    if (*current < low || *current > threshold)
        return false;
    return *previous < low || *previous > threshold;
}

// FUNC_AT(0x00050e10)
InputDevice* InputDevice::Construct(int port_) {
    vtable = kInputDeviceVtable;
    port = port_;
    return this;
}

// FUNC_AT(0x00050e30)
void InputDevice::Destruct() {
    vtable = kInputDeviceVtable;
}

// FUNC_AT(0x00050eb0)
InputDevice* InputDevice::DeletingDestructor(unsigned int flags) {
    vtable = kInputDeviceVtable;
    if (flags & 1)
        UMemory::FastFree(this, 0x1c);
    return this;
}

// AUTOINJECT
bool InputDevice::DeviceHasChanged() {
    int count = GetNumDeviceScalar();
    for (int i = 0; i < count; i++) {
        if (current[i] != previous[i])
            return true;
    }
    return false;
}

// AUTOINJECT
void InputDevice::DeadZoneChop(float *value, float low, float high) {
    if (*value > low && *value < high)
        *value = 0.0f;
}

// FUNC_AT(0x00051d90)
XBoxPadDevice* XBoxPadDevice::Construct(int port_) {
    InputDevice::Construct(port_);
    vtable = kXBoxPadDeviceVtable;
    for (int i = 0; i < kPadScalars; i++)
        scalarStorage[i].Construct();
    scalars = scalarStorage;
    previous = previousValues;
    current = currentValues;
    extra1 = extra1Values;
    extra2 = extra2Values;
    memset(previous, 0, sizeof(previousValues));
    memset(current, 0, sizeof(currentValues));
    memset(extra1, 0, sizeof(extra1Values));
    memset(extra2, 0, sizeof(extra2Values));
    return this;
}

// FUNC_AT(0x00051d80)
void XBoxPadDevice::Destruct() {
    vtable = kXBoxPadDeviceVtable;
    InputDevice::Destruct();
}

// FUNC_AT(0x00051e60)
XBoxPadDevice* XBoxPadDevice::DeletingDestructor(unsigned int flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, 0x56c);
    return this;
}

// AUTOINJECT
void XBoxPadDevice::Initialize() {
    static const struct { int type; const char *name; } kControls[kPadScalars] = {
        { DEVICE_SCALAR_AXIS, "ALX" },               { DEVICE_SCALAR_AXIS, "ALY" },
        { DEVICE_SCALAR_AXIS, "ARX" },               { DEVICE_SCALAR_AXIS, "ARY" },
        { DEVICE_SCALAR_ANALOG, "AButtonA" },        { DEVICE_SCALAR_ANALOG, "AButtonB" },
        { DEVICE_SCALAR_ANALOG, "AButtonX" },        { DEVICE_SCALAR_ANALOG, "AButtonY" },
        { DEVICE_SCALAR_ANALOG, "AButtonBlack" },    { DEVICE_SCALAR_ANALOG, "AButtonWhite" },
        { DEVICE_SCALAR_ANALOG, "AButtonLTrigger" }, { DEVICE_SCALAR_ANALOG, "AButtonRTrigger" },
        { DEVICE_SCALAR_DIGITAL, "DButtonUp" },      { DEVICE_SCALAR_DIGITAL, "DButtonDown" },
        { DEVICE_SCALAR_DIGITAL, "DButtonLeft" },    { DEVICE_SCALAR_DIGITAL, "DButtonRight" },
        { DEVICE_SCALAR_DIGITAL, "DButtonStart" },   { DEVICE_SCALAR_DIGITAL, "DButtonBack" },
        { DEVICE_SCALAR_DIGITAL, "DButtonLThumb" },  { DEVICE_SCALAR_DIGITAL, "DButtonRThumb" },
    };
    for (int i = 0; i < kPadScalars; i++)
        scalars[i].InitializeDeviceScalar(kControls[i].type, (char *)kControls[i].name, &previous[i], &current[i],
                                          &extra1[i], &extra2[i]);
}

// AUTOINJECT
void XBoxPadDevice::PollDevice() {
    const PadData *pad = PAD_getdataptr(port);
    memcpy(previous, current, sizeof(previousValues));

    // The sticks, with a second, wider deadzone than the pad library's.
    memcpy(current, pad->sticks, sizeof(pad->sticks));
    for (int i = 0; i < 4; i++)
        DeadZoneChop(&current[i], -0.4f, 0.4f);

    // The analogue buttons, 0 .. 255 scaled to 0 .. 1.
    for (int i = 0; i < 8; i++)
        current[4 + i] = (float)pad->analog[i] * (1.0f / 255.0f);

    // The digital buttons: the low byte of XINPUT's wButtons - d-pad up, down, left, right, START, BACK, the left
    // and right stick clicks - each 1 or 0.
    uint8_t digital = (uint8_t)pad->buttons;
    for (int i = 0; i < 8; i++)
        current[12 + i] = (digital & (1u << i)) ? 1.0f : 0.0f;
}

// AUTOINJECT
int XBoxPadDevice::GetNumDeviceScalar() {
    return kPadScalars;
}
