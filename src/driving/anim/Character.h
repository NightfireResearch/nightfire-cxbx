#ifndef DRIVING_ANIM_CHARACTER_H_
#define DRIVING_ANIM_CHARACTER_H_

// ---------------------------------------------------------------------------------------------------------------
// The actors' characters:
//   ActCharacterInfo (0xdc): a character's description string ("<number> <model> <textures...> <scale>") taken
//     apart into its tokens, with the model, texture and object file paths made from them.
//   ActCharacter (0x84, "ActCharacter"): an actor's model, textures and the two weapons it holds, drawn at one of
//     three levels of detail by distance, with a flattened shadow model under it and the muzzle flash as a light.
//   CharacterDrawOptions (0x60, invented name): the character's alpha and shadow settings, copied into
//     ActManager's own (+0x20) before each draw - the render methods read that copy.
// See Character.cpp; the character's event handlers are in Events.h.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../render/RSceneObj.hpp"
#include "../world/CollisionTypes.h"    // MATRIX4

class ActActor;
class ActModel;
class ActModelDatabase;
class ActTextureDatabase;
class ActWeapon;
class ActWeaponDatabase;
namespace EAGL {
struct TAR;
struct ViewPort;
}  // namespace EAGL

// The first letter of a description's model token (ActCharacterInfo::type)
enum CharacterType {
    kCharacterH = 0,                    // 'h'
    kCharacterP = 1,                    // 'p'
    kCharacterC = 2,                    // 'c'
    kCharacterA = 3,                    // 'a'
};

class ActCharacterInfo {                // 0xdc ("ActPoser" when ActCharacter allocates one)
public:
    int32_t number;                     // +0x00 the first token, one or two digits
    int32_t type;                       // +0x04 CharacterType; left as it was for any other letter
    char description[0x40];             // +0x08 a copy, cut up by strtok
    char *numberToken;                  // +0x48
    char *name;                         // +0x4c the model's name
    char *texture;                      // +0x50 types P and A: their one texture
    char *texture1;                     // +0x54 types H and C: their two
    char *texture2;                     // +0x58
    char *scaleToken;                   // +0x5c up to three digits, in hundredths
    float scale;                        // +0x60
    float inverseScale;                 // +0x64
    char modelPath[0x64];               // +0x68 data\actors\models\<name> and five characters lodSuffix rewrites
    char *texturePath;                  // +0xcc data\actors\textures\<texture>.xsh (new[0x64])
    char *texture1Path;                 // +0xd0
    char *texture2Path;                 // +0xd4
    char *lodSuffix;                    // +0xd8 the last five characters of modelPath

    ActCharacterInfo* Construct(const char *text);                              // 0x00015260
    void Destruct();                                                            // 0x00015600
    // modelPath ending "<lod + 1>.dat" and "<lod + 1>.rel"
    char* GetModelFileName(int lod);                                            // 0x00015640
    char* GetModelSymbolFileName(int lod);                                      // 0x00015680
};
static_assert(offsetof(ActCharacterInfo, numberToken) == 0x48 && offsetof(ActCharacterInfo, scale) == 0x60 &&
              offsetof(ActCharacterInfo, modelPath) == 0x68 && offsetof(ActCharacterInfo, texturePath) == 0xcc,
              "ActCharacterInfo layout");
static_assert(sizeof(ActCharacterInfo) == 0xdc, "an ActCharacterInfo is 0xdc bytes");

// A character's alpha and shadow settings (operator new; the name is ours). ActManager holds the copy at +0x20
// that the draws use; from its +0x20 on, that copy is what EAGL knows as "GAME::CharacterOptions".
struct CharacterDrawOptions {           // 0x60
    int32_t alpha;                      // +0x00 0..255
    float shadowColour[4];              // +0x04 a character's: (0.05, 0.05, 0.05, 1); the copy's: its first
                                        //       three times, then 1
    int32_t shadow;                     // +0x14 the copy's: drawing the shadow
    uint8_t unknown18[8];
    float alphaScale;                   // +0x20 the copy's: alpha / 255
    float unknown24;                    // +0x24 the copy's: shadowColour[0]
    float unknown28;                    // +0x28 the copy's: 1000000 for the shadow, else 0
    float unknown2c;                    // +0x2c the copy's: the same
    Coord3 shadowTriangle[3];           // +0x30 (ActCharacter::GetShadowTriangle)
    uint8_t unknown54[0xc];

    CharacterDrawOptions* Construct();  // inline in the original
    // Copies a character's options in, for its model (shadow 0) or its shadow (1)
    void Set(const CharacterDrawOptions *from, int shadow);                     // 0x00014940
};
static_assert(offsetof(CharacterDrawOptions, alphaScale) == 0x20 &&
              offsetof(CharacterDrawOptions, shadowTriangle) == 0x30, "CharacterDrawOptions layout");
static_assert(sizeof(CharacterDrawOptions) == 0x60, "the character options are 0x60 bytes");

class ActCharacter {                    // 0x84 ("ActCharacter")
public:
    enum { kWeapons = 2, kWeaponNameLength = 0x1e };

    ActModelDatabase *models;           // +0x00
    ActTextureDatabase *textures;       // +0x04
    ActWeaponDatabase *weaponDatabase;  // +0x08
    ActModel *model;                    // +0x0c
    EAGL::TAR *skins[3];                // +0x10 the info's texture, texture1, texture2 (NULL when it has none)
    EAGL::TAR *infraredSkin;            // +0x1c the texture "hhhh", on every skin in infrared mode
    ActCharacterInfo *info;             // +0x20
    ActWeapon *weapons[kWeapons];       // +0x24
    char weaponNames[kWeapons][kWeaponNameLength];  // +0x2c "NO WEAPON!" for none
    CharacterDrawOptions *options;      // +0x68
    bool hasShadow;                     // +0x6c
    float muzzleFlash;                  // +0x70 0..1
    Coord4 muzzleFlashDirection;        // +0x74 the flashing weapon's +0x124, as light 1's direction

    ActCharacter* Construct(ActModelDatabase *modelDatabase, ActTextureDatabase *textureDatabase,
                            ActWeaponDatabase *weapons, bool shadow, const char *description,
                            const char *weapon1, const char *weapon2);          // 0x000150a0
    void Destruct();                                                            // 0x00014700
    // Loads (into the databases) what the description and weapons name, to be used later
    static void LoadCharacter(const char *description, const char *weapon1, const char *weapon2);  // 0x00014490
    // The description's model and textures and the weapons in use; `weaponUse` is UseWeapon's second argument
    void ChangeCharacter(const char *description, const char *weapon1, const char *weapon2,
                         uint32_t weaponUse);                                   // 0x00014a50
    void StopUsingResources();                                                  // 0x00014680

    void ShowWeapon(int weapon);                                                // 0x00014580
    void HideWeapon(int weapon);                                                // 0x000145b0
    void SetWeaponsTransformToWorldSpace(const MATRIX4 *viewProjection,
                                         const MATRIX4 *inverse);               // 0x000145e0
    // Per second, stored per frame (a sixtieth)
    void SetWeaponVelocity(const Coord3 *velocity);                             // 0x00014610
    void PlayEvent(int weapon, uint32_t stimulus);                              // 0x00014740
    void SpawnWeapon(int weapon, Coord3 *position, Coord3 *direction, Coord3 *velocity,
                     Coord3 *spin);                                             // 0x000147f0
    // The first weapon's shell casings too
    void DrawWeapon(int weapon, EAGL::ViewPort *unused);                        // 0x00014820
    void SetWeaponBone(int weapon, const MATRIX4 *matrix);                      // 0x00014850
    Coord3* GetShadowTriangle();                                                // 0x00014870
    void SetAlpha(float alpha);                                                 // 0x00014bf0
    void GetScaleFactors(float *scale, float *inverse);                         // 0x00014c20
    // -1: no flash. Adds the weapon's flash strength, capped at 1.
    void CalculateMuzzleFlashIntensity(int weapon);                             // 0x00015010
    void InheritWeaponLightingFromCar();                                        // 0x00015180

    // The stencil state the shadow is drawn with, and its end
    void StartShadow();                                                         // 0x00014760
    void EndShadow();                                                           // 0x000147d0
    // Draws the model at a level of detail by the view distance of `cullMatrix`, and its shadow; answers that
    // level (before a one-model character's is taken as 0), or 2 when it is not drawn
    int Draw(const MATRIX4 *cullMatrix, const MATRIX4 *matrix);                 // 0x00014c50
};
static_assert(offsetof(ActCharacter, weaponNames) == 0x2c && offsetof(ActCharacter, options) == 0x68 &&
              offsetof(ActCharacter, muzzleFlashDirection) == 0x74, "ActCharacter layout");
static_assert(sizeof(ActCharacter) == 0x84, "an ActCharacter is 0x84 bytes");

// ---- helpers the linker placed among the character's code (names ours)

// RSceneObj::brightness from a level clamped to 0..1. Provisional home: RSceneObj (render/RSceneObj.hpp).
class RSceneObjBrightness : public RSceneObj {
public:
    void SetBrightness(float level);                                            // 0x000143e0
};

// A Coord4, returned by value
Coord4* MakeCoord4(Coord4 *result, float x, float y, float z, float w);         // 0x00014440

// std::min<float>
const float* MinFloat(const float *a, const float *b);                          // 0x00014880

// EAGL::LightBlock (0x70, "EAGL::LightBlock new"): four lights' directions as rows of x, y and z, and their colours
struct LightBlock {
    float directions[3][4];             // +0x00
    Coord4 colours[4];                  // +0x30

    // The directions' x, y and z only; the colour comes back as (w, x, y, z)
    void GetLight(int light, Coord4 *direction, Coord4 *colour);                // 0x000148a0
    void SetLight(int light, const Coord4 *direction, const Coord4 *colour);    // 0x000148f0
};
static_assert(sizeof(LightBlock) == 0x70, "a LightBlock is 0x70 bytes");

// The lighting manager (RLightManager) as the characters and ActManager use it
struct LightingView {
    uint8_t unknown000[0xec];
    LightBlock lights;                  // +0xec the scene's lights
};

#define Lighting (*(LightingView **)0x001ec260)

#endif // DRIVING_ANIM_CHARACTER_H_
