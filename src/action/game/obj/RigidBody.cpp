#include "RigidBody.h"
#include "Gas.h"
#include "Light.h"
#include "build.h"
#include "control.h"
#include "object.h"
#include "../../game.h"
#include "../../engine/celglist.h"
#include "../../engine/viewer.h"
#include "../../math/X87Ops.h"
#include "../../sound/Sound.h"
#include "../../util/Random.h"

#include <math.h>
#include <string.h>
#include "Env.h"

#pragma fp_contract(off)

static const float kImpulseClamp = 4.0f;
static const float kRestingEpsilon = 0.0002f;

// AUTOINJECT
void RB_Init(RIGIDBODY *rb, ushort kind) {
    memset(rb, 0, sizeof(RIGIDBODY));
    rb->kind = kind;
    rb->quat.q[0] = 1.0f;
    rb->quat.q[1] = 0.0f;
    rb->quat.q[2] = 0.0f;
    rb->quat.q[3] = 0.0f;
    if (kind == RB_FREE) {
        rb->restitution = -0.5f;
        rb->linDamp = 1.0f;
        rb->angDamp = 31.0f;
    } else {
        rb->linDamp = 0.0f;
        rb->restitution = -0.75f;
        rb->angDamp = kind == RB_SWING_Y ? 250.0f : 25.0f;
    }
}

// The moments of inertia must be positive and each at most the sum of the other two
// AUTOINJECT
bool RB_Build(RIGIDBODY *rb, _VECTOR *gravity, float *inertia, float mass) {
    if (inertia[0] < 0.0f || inertia[1] < 0.0f || inertia[2] < 0.0f) {
        NF_WARN("TOI_body cannot be negative.\n");    // GC check (0x8005ea54)
        return false;
    }
    if ((double)inertia[1] + inertia[0] < inertia[2] || (double)inertia[2] + inertia[1] < inertia[0] ||
        (double)inertia[2] + inertia[0] < inertia[1]) {
        NF_WARN("TOI_body does not follow triangle rule.\n");    // GC check (0x8005ea54)
        return false;
    }
    rb->inertia[0] = inertia[0];
    rb->inertia[1] = inertia[1];
    rb->inertia[2] = inertia[2];
    rb->invInertia[0] = 1.0f / inertia[0];
    rb->invInertia[2] = 1.0f / inertia[1];
    rb->invInertia[1] = 1.0f / inertia[2];
    rb->mass = mass;
    rb->gravity = gravity;
    rb->invMass = 1.0f / mass;
    return true;
}

// AUTOINJECT
void RB_AddImpulsiveForceAtPoint(RIGIDBODY *rb, _VECTOR *force, _VECTOR *point) {
    _VECTOR arm, torque;
    Vec_Subtract(point, Mat_Position(*rb->matrix), &arm);
    Vec_Cross(&arm, force, &torque);
    Vec_Add2(force, &rb->impulse, &rb->impulse);
    Vec_Add2(&rb->angImpulse, &torque, &rb->angImpulse);
}

// The drag on the spin, and the body's acceleration: gravity, less the drag on its velocity. Our name (Ghidra had
// RB_unknownHelperFunc2); angAccel is not read.
// AUTOINJECT
void RB_Drag(RIGIDBODY *rb, _VECTOR *accel, _VECTOR *angAccel, float dt) {
    if (rb->angDamp > 0.0f) {
        double inertia = (((double)rb->inertia[2] + rb->inertia[1]) + rb->inertia[0]) * 0.333f;
        Extended decay = FExp(inertia / rb->angDamp * (-1.0 / inertia) * dt);
        float k = (float)-((Extended(1.0f) - decay) / Extended(dt));
        auxVec_AddMulR32(&rb->omega, &rb->omega, k, &rb->omega);
    }
    if (rb->kind == RB_FREE) {
        if (rb->linDamp > 0.0f) {
            Extended decay = FExp(-1.0 / rb->linDamp * dt);
            float k = (float)((Extended(1.0f) - decay) * Extended(-1.0 / dt));
            RB_VecSpecial(accel, k, &rb->vel, rb->gravity, rb->linDamp);
        } else {
            Vec_Copy(&GRAVITY_VECTOR, accel);
        }
    }
}

// The body moves by its velocity and turns by its spin. Our name (Ghidra had RB_unknownHelperFunc).
// AUTOINJECT
void RB_Move(RIGIDBODY *rb, float dt) {
    if (rb->kind == RB_FREE)
        auxVec_AddMulR32(Mat_Position(*rb->matrix), &rb->vel, dt, Mat_Position(*rb->matrix));
    if (!Vec_IsEqual(&rb->omega, &CONST_ZERO_VECTOR, kRestingEpsilon))
        RB_apply_omega(&rb->quat, &rb->omega, dt);
}

// AUTOINJECT
void RB_ApplyImpulses(RIGIDBODY *rb) {
    if (!Vec_IsEqual(&rb->angImpulse, &CONST_ZERO_VECTOR, kRestingEpsilon)) {
        _VECTOR spin, world;
        RB_ToBody(&spin, &rb->angImpulse, rb->matrix);
        spin.x = spin.x * rb->invInertia[0];
        spin.y = spin.y * rb->invInertia[1];
        spin.z = spin.z * rb->invInertia[2];
        if (spin.x < -kImpulseClamp)
            spin.x = -kImpulseClamp;
        else if (spin.x > kImpulseClamp)
            spin.x = kImpulseClamp;
        if (spin.y < -kImpulseClamp)
            spin.y = -kImpulseClamp;
        else if (spin.y > kImpulseClamp)
            spin.y = kImpulseClamp;
        if (spin.z < -kImpulseClamp)
            spin.z = -kImpulseClamp;
        else if (spin.z > kImpulseClamp)
            spin.z = kImpulseClamp;
        RB_ToWorld(&world, &spin, rb->matrix);
        Vec_Add2(&rb->omega, &world, &rb->omega);
    }
    if (rb->kind == RB_FREE)
        auxVec_AddMulR32(&rb->vel, &rb->impulse, rb->invMass, &rb->vel);
}

// The swinging bodies' collision: a cylinder from below the pivot to the body's end against the geometry (not for
// RB_SWING_Y), and the bullets' hits
// AUTOINJECT
void RB_RestingContactRot(obj_tag *obj, RIGIDBODY *rb, HITDATA_tag *hit) {
    HITDATA_tag *hits = NULL;
    _VECTOR up;
    Mat_GetUp(&up, &obj->transformMatrix);
    _VECTOR *pivot = Mat_Position(obj->transformMatrix);
    auxVec_AddMulR32(pivot, &up, -(rb->halfHeight - rb->test.radius), &rb->test.pos);
    if (rb->kind != RB_SWING_Y) {
        auxVec_AddMulR32(pivot, &up, -(rb->test.radius * 1.8f), &rb->test.rayStart);
        auxVec_AddMulR32(pivot, &up, -(rb->halfHeight - rb->test.radius), &rb->test.rayEnd);
        rb->test.mask = 0x10;
        Collide_CylinderIntersect(&rb->test, &hits, 0, 0);
    }
    for (; hit != NULL; hit = hit->next) {
        if (hit->hitObj == NULL || hit->hitObj->objectType != OBJECTTYPE_BULLET)
            continue;
        Sound_Play3D((Action_SFX)rb->sound, pivot, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        _VECTOR force, arm, torque;
        Vec_MulR32(&force, &hit->hitDirection, hit->dmgAmt * 0.5f);
        Vec_Subtract(&hit->hitPosition, Mat_Position(*rb->matrix), &arm);
        Vec_Cross(&arm, &force, &torque);
        if (rb->kind == RB_SWING_X) {
            torque.y = 0.0f;
            torque.z = 0.0f;
        }
        if (rb->kind == RB_SWING_Y) {
            torque.z = 0.0f;
            torque.x = 0.0f;
        }
        if (rb->kind == RB_SWING_Z) {
            torque.y = 0.0f;
            torque.x = 0.0f;
        }
        if (rb->kind == RB_SWING_XZ)
            torque.y = 0.0f;
        Vec_Add2(&rb->angImpulse, &torque, &rb->angImpulse);
    }
    if (hits != NULL && !Vec_IsEqual(&rb->test.push, &CONST_ZERO_VECTOR, 0.0f)) {
        _VECTOR push, arm, torque;
        Vec_MulR32(&push, &rb->test.push, 2.5f);
        Vec_Subtract(&hits->hitPosition, Mat_Position(*rb->matrix), &arm);
        Vec_Cross(&arm, &push, &torque);
        torque.y = 0.0f;
        Vec_Add2(&rb->angImpulse, &torque, &rb->angImpulse);
    }
    Collide_FreeHitList(&hits);
}

// The friction of a contact: in the basis of the hit's normal, each part of the velocity above 0.05 turns the body
// AUTOINJECT
void RB_CalcTorque(RIGIDBODY *rb, HITDATA_tag *hit) {
    _VECTOR normal, arm, u, v, torque;
    Vec_Copy(&hit->surfaceNormal.normal, &normal);
    Vec_Subtract(&hit->hitPosition, Mat_Position(*rb->matrix), &arm);
    Vec_MakeOrthonormalBasis(&u, &v, &normal);
    // The first sum is compared before it is stored, the other two after
    double alongU = ((double)u.y * rb->vel.y + (double)u.z * rb->vel.z) + (double)u.x * rb->vel.x;
    float alongUStored = (float)alongU;
    float alongN = (float)(((double)normal.y * rb->vel.y + (double)normal.z * rb->vel.z) + (double)normal.x * rb->vel.x);
    float alongV = (float)(((double)v.y * rb->vel.y + (double)v.z * rb->vel.z) + (double)v.x * rb->vel.x);
    if (AbsFchs(alongU) > 0.05f) {
        Vec_Cross(&arm, &u, &torque);
        auxVec_AddMulR32(&rb->angImpulse, &torque, alongUStored * -0.01f, &rb->angImpulse);
    }
    if (AbsFchs(alongV) > 0.05f) {
        Vec_Cross(&arm, &v, &torque);
        auxVec_AddMulR32(&rb->angImpulse, &torque, alongV * -0.01f, &rb->angImpulse);
    }
    if (AbsFchs(alongN) > 0.05f) {
        Vec_Cross(&arm, &normal, &torque);
        auxVec_AddMulR32(&rb->angImpulse, &torque, alongN * -0.2f, &rb->angImpulse);
    }
}

// One step of dt: the tick's impulses (unless skipImpulses), the drag, the velocities, the move; a swinging body
// is given the torque that turns it back upright for the next tick
// AUTOINJECT
void RB_EulerEvolve(RIGIDBODY *rb, float dt, int skipImpulses) {
    if (!skipImpulses) {
        RB_ApplyImpulses(rb);
        Vec_Zero(&rb->impulse);
        Vec_Zero(&rb->angImpulse);
    }
    if (dt != 0.0f) {
        RB_Drag(rb, &rb->accel, &rb->angAccel, dt);
        auxVec_AddMulR32(&rb->omega, &rb->angAccel, dt, &rb->omega);
        if (rb->kind == RB_FREE)
            auxVec_AddMulR32(&rb->vel, &rb->accel, dt, &rb->vel);
        RB_Move(rb, dt);
    }
    Quat_QuaternionToMatrix(rb->matrix, &rb->quat);
    if (rb->kind != RB_FREE) {
        _VECTOR up, torque;
        Mat_GetUp(&up, rb->matrix);
        double k = (double)FRAME_RATE_MUL * rb->inertia[1];
        torque.x = (float)-(up.z * k);
        torque.y = 0.0f;
        torque.z = (float)(k * up.x);
        Vec_Add2(&rb->angImpulse, &torque, &rb->angImpulse);
        Quat_QuaternionToMatrix(rb->matrix, &rb->quat);
    }
}

// The collision handler (control_funcs)
// AUTOINJECT
void RB_CollisionHandler(obj_tag *obj) {
    if (obj == NULL)
        return;
    RIGIDBODY *rb = (RIGIDBODY *)obj->extraObjectData;
    if (rb->kind == RB_FREE)
        RB_RestingContact(obj, rb, obj->hitList);
    else
        RB_RestingContactRot(obj, rb, obj->hitList);
    obj->transformFlags |= TRANSFORM_MOVED;
}

// A free body's collision with its hits: friction, a bullet's push (the first ends the list), an object that moved
// less than 0.707 carries the body with it; then the geometry's push takes the velocity into the surface away
// AUTOINJECT
void RB_RestingContact(obj_tag *obj, RIGIDBODY *rb, HITDATA_tag *hit) {
    for (; hit != NULL; hit = hit->next) {
        if (rb->testType != HITTEST_POINT)
            RB_CalcTorque(rb, hit);
        obj_tag *other = hit->hitObj;
        if (other == NULL)
            continue;
        if (other->objectType == OBJECTTYPE_BULLET) {
            Sound_Play3D((Action_SFX)rb->sound, Mat_Position(obj->transformMatrix), 100.0f, -1.0f, -1.0f, 0, 0, 0);
            _VECTOR force;
            Vec_MulR32(&force, &hit->hitDirection, hit->dmgAmt * 0.5f);
            RB_AddImpulsiveForceAtPoint(rb, &force, &hit->hitPosition);
            break;
        }
        if (other->objectType == OBJECTTYPE_PLAYER || other->objectType == OBJECTTYPE_RIGIDBODY)
            continue;
        _VECTOR moved;
        Vec_Subtract(Mat_Position(other->transformMatrix), &other->lastPosition, &moved);
        if (Vec_SqMagnitude(&moved) < 0.5f)
            Vec_Add2(&moved, Mat_Position(*rb->matrix), Mat_Position(*rb->matrix));
    }
    if (Vec_IsEqual(&rb->test.push, &CONST_ZERO_VECTOR, 0.0f))
        return;
    _VECTOR normal;
    Vec_Normalise(&normal, &rb->test.push);
    double into = ((double)normal.y * rb->vel.y + (double)normal.z * rb->vel.z) + (double)normal.x * rb->vel.x;
    auxVec_AddMulR32(&rb->vel, &normal, (float)-(into * ((double)rb->restitution + 1.0f)), &rb->vel);
    Vec_Add2(&rb->test.push, Mat_Position(*rb->matrix), Mat_Position(*rb->matrix));
}

// The wind in the body's cel, as a force: at the centre of a free body, at the far end of a swinging one
// AUTOINJECT
void RB_ApplyWind(RIGIDBODY *rb, obj_tag *obj) {
    _VECTOR wind;
    _VECTOR *pos = Mat_Position(obj->transformMatrix);
    Env_GetWindVel(obj->inCel, pos, &wind);
    double k = (double)REC_FRAME_RATE * rb->invMass;
    wind.x = (float)(wind.x * k);
    wind.y = (float)(wind.y * k);
    wind.z = (float)(wind.z * k);
    if (rb->kind == RB_FREE) {
        Vec_Add2(&wind, &rb->impulse, &rb->impulse);    // RB_AddImpulsiveForce
        return;
    }
    _VECTOR point;
    point.x = pos->x;
    point.y = (float)(((double)pos->y + rb->halfHeight) - rb->test.radius);
    point.z = pos->z;
    RB_AddImpulsiveForceAtPoint(rb, &wind, &point);
}

// The rigid body object (OBJECTTYPE_RIGIDBODY): a cuboid of the model's box and the placement's mass (3 for none),
// starting upright. A free body tests a sphere of the model's radius; a swinging body is tested by
// RB_RestingContactRot only, with the radius of its narrower side (RB_SWING_X, RB_SWING_Z), the model's (RB_SWING_Y)
// or half its diagonal across x and z.
// AUTOINJECT
obj_tag* RB_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *lvl) {
    RigidBodyPlacement *keys = (RigidBodyPlacement *)lvl;
    obj_tag *obj = control_create_object(sizeof(RIGIDBODY), pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    RIGIDBODY *rb = (RIGIDBODY *)obj->extraObjectData;
    RB_Init(rb, keys->kind);
    obj->transformFlags |= TRANSFORM_MATRIX_PLACED | TRANSFORM_POSITION_FROM_MATRIX;
    obj->objectType = OBJECTTYPE_RIGIDBODY;
    obj->hitTest = &rb->test;
    obj->objGraphics = celgl;
    rb->matrix = &obj->transformMatrix;
    float mass = keys->mass != 0 ? keys->mass : 3.0f;
    float sizeX = celgl->extentMax.x - celgl->extentMin.x;
    float sizeZ = celgl->extentMax.z - celgl->extentMin.z;
    float inertia[3];
    RB_CuboidMomentsOfInertia(inertia, mass, sizeX, celgl->extentMax.y - celgl->extentMin.y, sizeZ);
    rb->halfHeight = (float)AbsFchs(celgl->extentMin.y);
    RB_Build(rb, rb->kind == RB_FREE ? &GRAVITY_VECTOR : &CONST_ZERO_VECTOR, inertia, mass);
    Quat_QuaternionToMatrix(rb->matrix, &rb->quat);     // RB_SetState
    rb->test.ignoreA = obj;
    build_LinkToRoom(obj, 0, glb_world);
    rb->testType = HITTEST_SPHERE;
    rb->test.radius = celgl->boundSphere.r;
    switch (rb->kind) {
    case RB_FREE:
        break;
    case RB_SWING_X:
    case RB_SWING_Z:
        rb->testType = 0;
        rb->test.radius = sizeX < sizeZ ? sizeX : sizeZ;
        break;
    case RB_SWING_Y:
        rb->testType = 0;
        break;
    default:
        rb->testType = 0;
        rb->test.radius = (float)(sqrt((double)sizeZ * sizeZ + (double)sizeX * sizeX) * 0.5f);
        break;
    }
    rb->carriesLight = keys->carriesLight;
    rb->sound = keys->sound != 0 ? keys->sound : 0xffff;
    return obj;
}

// The update (control_funcs): under the cel's height (cel_tag::shearHeight), 15 frames in 16, a bubble and a lift
// of |y - the cel's top| x mass x 0.4 plus a random 0 to 0.15; the wind; a step of a frame; the test follows the
// body, and a light the body carries follows the test
// AUTOINJECT
void RB_Update(obj_tag *obj) {
    if (obj == NULL)
        return;
    RIGIDBODY *rb = (RIGIDBODY *)obj->extraObjectData;
    if (rb == NULL || obj->objGraphics == NULL)
        return;
    rb->test.type = 0;
    if (rb->kind == RB_FREE)
        rb->test.type = rb->testType;
    cel_tag *cel = obj->inCel;
    if (cel != NULL && cel->shearHeight > Mat_Position(obj->transformMatrix)->y &&
        (GameState.NumFramesUnpaused & 0xf) != 0) {
        _VECTOR lift = {0.0f, 0.0f, 0.0f};
        float depth = (float)AbsFchs((double)Mat_Position(obj->transformMatrix)->y - cel->someMax.y);
        double random = Float_FRand(0.15f);
        lift.y = (float)((double)depth * rb->mass * 0.4f + random);
        Gas_Init(obj->inCel, Mat_Position(obj->transformMatrix), NULL, NULL, 0.5f, 1.0f, HT_Entity_RippleGas, 1,
                 0x40008080, 0x80808008, 0x10);
        Vec_Add2(&lift, &rb->impulse, &rb->impulse);    // RB_AddImpulsiveForce
    }
    RB_ApplyWind(rb, obj);
    RB_EulerEvolve(rb, REC_FRAME_RATE, 0);
    rb->test.cel = obj->inCel;
    rb->test.type = rb->testType;
    if (rb->kind != RB_FREE) {
        if (rb->carriesLight && rb->light == NULL)
            rb->light = Light_FindMapLight(&rb->test.pos, rb->test.radius);
        if (rb->light != NULL)
            Vec_Copy(&rb->test.pos, &rb->light->position);
    } else {
        Vec_Copy(Mat_Position(obj->transformMatrix), &rb->test.pos);
    }
    obj->transformFlags |= TRANSFORM_MOVED;
}

// A cuboid of the mass and sides, about its centre
// AUTOINJECT
void RB_CuboidMomentsOfInertia(float *out, float mass, float sizeX, float sizeY, float sizeZ) {
    double y2 = (double)sizeY * sizeY;
    double z2 = (double)sizeZ * sizeZ;
    double x2 = (double)sizeX * sizeX;
    out[0] = (float)((z2 + y2) * mass * 0.083333336f);
    out[1] = (float)((x2 + z2) * mass * 0.083333336f);
    out[2] = (float)(((float)x2 + y2) * mass * 0.083333336f);     // x2 stored over sizeY, and read back
}

// v by the matrix's rotation, body to world
// AUTOINJECT
void RB_ToWorld(_VECTOR *out, _VECTOR *v, _MATRIX *mtx) {
    out->x = (float)(((double)mtx->m[8] * v->z + (double)mtx->m[4] * v->y) + (double)v->x * mtx->m[0]);
    out->y = (float)(((double)mtx->m[9] * v->z + (double)mtx->m[5] * v->y) + (double)mtx->m[1] * v->x);
    out->z = (float)(((double)mtx->m[10] * v->z + (double)mtx->m[6] * v->y) + (double)mtx->m[2] * v->x);
}

// v by the transpose of the matrix's rotation, world to body
// AUTOINJECT
void RB_ToBody(_VECTOR *out, _VECTOR *v, _MATRIX *mtx) {
    out->x = (float)(((double)mtx->m[1] * v->y + (double)mtx->m[2] * v->z) + (double)v->x * mtx->m[0]);
    out->y = (float)(((double)mtx->m[5] * v->y + (double)mtx->m[4] * v->x) + (double)mtx->m[6] * v->z);
    out->z = (float)(((double)mtx->m[9] * v->y + (double)mtx->m[8] * v->x) + (double)mtx->m[10] * v->z);
}

// Turns q by the spin omega for dt
// AUTOINJECT
void RB_apply_omega(quaternion_tag *q, _VECTOR *omega, float dt) {
    double speed = Vec_Magnitude(omega);
    if (speed == 0.0)
        return;
    double half = dt * speed * 0.5f;
    quaternion_tag turn, from;
    turn.q[0] = (float)FCos(half);
    Extended s = FSin(half) / Extended(speed);
    turn.q[1] = (float)(s * Extended(omega->x));
    turn.q[2] = (float)(s * Extended(omega->y));
    turn.q[3] = (float)(s * Extended(omega->z));
    Quat_Copy(&from, q);
    Quat_QuaternionMultiply(&turn, &from, q);
}

// out = (a - b s) k
// AUTOINJECT
void RB_VecSpecial(_VECTOR *out, float k, _VECTOR *a, _VECTOR *b, float s) {
    out->x = (float)((a->x - (double)s * b->x) * k);
    out->y = (float)((a->y - (double)s * b->y) * k);
    out->z = (float)((a->z - (double)s * b->z) * k);
}
