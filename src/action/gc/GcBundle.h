#ifndef GC_GCBUNDLE_H_
#define GC_GCBUNDLE_H_

// The GameCube's level bundles (gc\<hash>.bin, docs/gc-data.md) made the Xbox's, whole, before the loader sees them:
//
// - a level's bundle (0x0700xxxx) is EDL-compressed (big-endian EDL, which the Xbox's EDL code reads); the others are
//   not;
// - the header's two words are big-endian, the directory's entries little-endian as the Xbox's;
// - skins, anim scripts, skeletons, game scripts and menus (types 3, 5, 6, 7, 8) are the Xbox's byte for byte;
// - the map packs (types 0, 1, 2, 11) are converted (GcMap.h);
// - the anim sequences (type 4, byte for byte the Xbox's) are not in the level's bundle but in a bundle shared by a
//   group of levels (0x07e0000n, the group from common.bin) and one of the level's own (0x07d0xxxx): they are added,
//   as the Xbox's level bundles hold them;
// - the cutscenes' loadable entries (type 12, a binary hashcode for a name, which the Xbox's bundles do not have) are
//   given the hashcode as a name and their data the Xbox's order, for LoaderProcess's LoadableFiles;
// - the meshes' data ("GC EXTRA STATIC", type 13, and "GC EXTRA DISCARD", type 14, which the converted meshes have
//   taken what they need from) is left out;
// - 0x07f0xxxx, a level's sequences alone (the loadables' reload, GcLoader.cpp), is made from its sequence bundles.
//
// No game state: the offline checker uses it too.

#include <stdint.h>
#include <string>
#include <vector>

typedef std::vector<uint8_t> GcBytesVec;

// A bundle's directory entry: where its data is in the (decompressed) bundle
struct GcBundleEntry {
    std::string name;       // as stored (a type 12 entry's is its hashcode in hex)
    int type;
    uint32_t offset;
    uint32_t length;
};

// The header and directory are big-endian (the GameCube's) or little (the Xbox's), told by the file count; a loadable
// entry's name is four bytes in the GameCube's, a string in the Xbox's
bool GcBundle_Directory(const uint8_t *bundle, uint32_t bytes, std::vector<GcBundleEntry> &entries);

// A bundle file as it is on the disc to its bytes: EDL-compressed or not. False when it cannot be decompressed.
bool GcBundle_Decompress(const GcBytesVec &file, GcBytesVec &out);

// The bundle of shared sequences for a level, from common.bin's pairs (level, group; big-endian): 0 for none
uint32_t GcBundle_SequenceGroup(const uint8_t *commonBin, uint32_t bytes, uint32_t level);

// What a conversion did
struct GcBundleStats {
    int entries;            // in the Xbox's bundle
    int packs;
    int sequencesAdded;
    int dropped;            // types 13 and 14
    int loadables;          // type 12 entries
    int texturesConverted;
    int texturesFailed;
    int duplicates;
    int meshesConverted;
    int meshesSkinned;
    int meshesSkipped;
    int unknownBlocks;
};

// A decompressed GameCube bundle as the Xbox's, with the type 4 entries of the sequence bundles (decompressed too)
// added where the name is not already there
void GcBundle_Convert(const uint8_t *bundle, uint32_t bytes, const std::vector<GcBytesVec> &sequenceBundles,
                      GcBytesVec &out, GcBundleStats *stats);

// Reads a file of the GameCube's disc whole (path from the disc's root, '\' separated, as GcDisc_Path gives): false
// when there is none
typedef bool (*GcReadFile)(void *context, const char *path, GcBytesVec &out);

// The bundle of the hashcode as the Xbox's: read, decompressed and converted; for a level (0x0700xxxx) its sequence
// bundles too, the group's from common.bin and the level's own (0x07d0xxxx); for 0x07f0xxxx those sequences
// alone. False when the bundle is not on the disc, is empty there (07000001) or does not convert.
bool GcBundle_Load(uint32_t hash, GcReadFile read, void *context, GcBytesVec &out, GcBundleStats *stats);

#endif // GC_GCBUNDLE_H_
