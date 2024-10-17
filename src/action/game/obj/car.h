#ifndef CAR_H_
#define CAR_H_

void Car_Init(void);
obj_tag * Car_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *level);

#endif // CAR_H_