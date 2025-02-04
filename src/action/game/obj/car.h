#ifndef CAR_H_
#define CAR_H_

void Car_Init(void);
obj_tag * Car_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *level);
void Car_PlayerHasDied(obj_tag *player);
void Car_Activate(obj_tag* carObj, obj_tag* playerObj);


#endif // CAR_H_