#include "view.h"

#include <math.h>

// AUTOGEN
void View_SetDrawInViews(obj_tag* o, short views);

// Visible to everyone
// AUTOINJECT
void View_SetDrawInAllViews(obj_tag* o) {
    View_SetDrawInViews(o, 0x1f);
}

// Visible to nobody
// AUTOINJECT
void View_SetDrawInNoViews(obj_tag* o) {
    View_SetDrawInViews(o, 0);
}

// Visible to everyone except the specified player (eg 3rd person view of a gun model)
// AUTOINJECT
void View_SetDrawInOtherViewsOnly(obj_tag* param_1, char playerNum) {
    char views = 0x1f & (~(1 << (playerNum & 0x1f)));
    View_SetDrawInViews(param_1, views);
}

// Visible to the specified player only (eg 1st person view of a gun model)
// AUTOINJECT
void View_SetDrawInThisViewOnly(obj_tag *param_1, char playerNum) {
    char views = (1 << (playerNum & 0x1f));
    View_SetDrawInViews(param_1, views);
}

// AUTOINJECT
void View_RotTransMatrix(_VECTOR *rotation, _VECTOR *position, _MATRIX *matrix) {
  
  float cos_rx = cosf(rotation->x);
  float sin_rx = sinf(rotation->x);
  float cos_ry = cosf(rotation->y);
  float sin_ry = sinf(rotation->y);
  float cos_rz = cosf(rotation->z);
  float sin_rz = sinf(rotation->z);

  matrix->m[0] = (cos_rz * cos_ry);
  matrix->m[1] = sin_rz;
  matrix->m[2] = -(cos_rz * sin_ry);
  matrix->m[4] = (sin_ry * sin_rx - sin_rz * cos_ry * cos_rx);
  matrix->m[5] = (cos_rz * cos_rx);
  matrix->m[6] = (cos_ry * sin_rx + sin_rz * sin_ry * cos_rx);
  matrix->m[8] = (sin_ry * cos_rx + sin_rz * cos_ry * sin_rx);
  matrix->m[9] = -(cos_rz * sin_rx);
  matrix->m[10] = (cos_ry * cos_rx - sin_rz * sin_ry * sin_rx);
  matrix->m[0xc] = position->x;
  matrix->m[0xd] = position->y;
  matrix->m[0xe] = position->z;
  return;
}
