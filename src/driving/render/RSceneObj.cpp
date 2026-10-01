#include "RSceneObj.hpp"

// RSceneObj::flags bit 0.
static const uint8_t SCENEOBJ_VISIBLE = 0x01;

// AUTOINJECT
void RSceneObj::Show() {
    flags |= SCENEOBJ_VISIBLE;
}

// AUTOINJECT
void RSceneObj::Hide() {
    flags &= (uint8_t)~SCENEOBJ_VISIBLE;
}
