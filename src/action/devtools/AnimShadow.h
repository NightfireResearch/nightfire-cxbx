#ifndef ACTION_DEVTOOLS_ANIMSHADOW_H_
#define ACTION_DEVTOOLS_ANIMSHADOW_H_

// Shadow tests for the animation reimplementations (engine/Anim.cpp). Called by Inject() after the patches are
// written; does nothing unless settings.ini has AnimShadowTest=on.
void AnimShadow_Install(void);

#endif // ACTION_DEVTOOLS_ANIMSHADOW_H_
