#ifndef DRIVING_DEVTOOLS_SCENEOBJSHADOW_H_
#define DRIVING_DEVTOOLS_SCENEOBJSHADOW_H_

// NIGHTFIRE_SCENEOBJSHADOW=1: render/WorldCulling.cpp, render/RSceneObj.cpp's queries, transforms and culling walk,
// and RRenderHigh::RearrangeSplitScreens against the originals. See SceneObjShadow.cpp. Run from the first
// simulation tick, once the track, its scene objects and the views exist.
void SceneObjShadow_Run(void);

#endif // DRIVING_DEVTOOLS_SCENEOBJSHADOW_H_
