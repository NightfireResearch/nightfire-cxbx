// The GameCube's driving data against the Xbox's, offline (docs/gc-driving.md): no game, no window.
//
//   gcdrive_check <gc disc> <xbox disc> files     every archive entry against its Xbox twin, by archive kind and
//                                                 extension: the same bytes (unpacked), different, on one disc only
//   gcdrive_check <gc disc> <xbox disc> udata     every .crp and .gal: the record headers turned into the Xbox's
//                                                 (GcUData.h), then the trees compared with the Xbox twins', and
//                                                 each payload classed (the same, every word turned round, other)
//   gcdrive_check <gc disc> <xbox disc> objects   the EAGL objects (.o, .dat + .rel, the .crp files' ELFd + ELFr):
//                                                 the symbols' classes and the names nothing in the object defines
//   gcdrive_check <gc disc> <xbox disc> sound     the PT headers of every .asf stream and .bnk patch: codec, rate
//
// A disc is the folder holding driving\ (the GameCube's also holds driving.elf). The GameCube's .gsh and .gfn are
// paired with the Xbox's .xsh and .xfn of the same name.

#include "../gc/GcConvert.h"
#include "../gc/GcPcm.h"
#include "../gc/GcUData.h"
#include "../gc/GcViv.h"
#include "../../common/sound/xadpcm.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

typedef std::vector<uint8_t> Bytes;

const char *const kArchives[] = {
    "misc.viv", "race.viv",
    "mis01.viv", "mis3.viv", "mis4.viv", "mis11.viv", "mis13a.viv", "mis13b.viv", "mis13c.viv",
    "mis01.mus", "mis3.mus", "mis4.mus", "mis11.mus", "mis13a.mus", "mis13b.mus", "mis13c.mus",
    "mis01en.spe", "mis3en.spe", "mis4en.spe", "mis11en.spe", "mis13aen.spe", "mis13ben.spe", "mis13cen.spe",
};

uint32_t Be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

uint32_t Le32(const uint8_t *p) {
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

uint16_t Le16(const uint8_t *p) {
    return (uint16_t)(p[1] << 8 | p[0]);
}

std::string Lower(std::string s) {
    for (char &c : s)
        c = (char)tolower((unsigned char)c);
    return s;
}

std::string Extension(const std::string &name) {
    size_t dot = name.find_last_of('.');
    size_t slash = name.find_last_of('\\');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return "";
    return Lower(name.substr(dot));
}

// The Xbox twin's name of a GameCube entry
std::string XboxName(const std::string &gcName) {
    std::string ext = Extension(gcName);
    std::string stem = gcName.substr(0, gcName.size() - ext.size());
    if (ext == ".gsh")
        return stem + ".xsh";
    if (ext == ".gfn")
        return stem + ".xfn";
    return gcName;
}

// The GameCube twin's name of an Xbox entry
std::string GcName(const std::string &xboxName) {
    std::string ext = Extension(xboxName);
    std::string stem = xboxName.substr(0, xboxName.size() - ext.size());
    if (ext == ".xsh")
        return stem + ".gsh";
    if (ext == ".xfn")
        return stem + ".gfn";
    return xboxName;
}

std::string ArchiveKind(const char *archive) {
    return strrchr(archive, '.') + 1;
}

struct Discs {
    std::string gc, xbox;
};

bool OpenPair(const Discs &discs, const char *archive, GcViv &gc, GcViv &xbox) {
    std::string g = discs.gc + "\\driving\\" + archive, x = discs.xbox + "\\driving\\" + archive;
    if (!gc.Open(g)) {
        printf("cannot open %s\n", g.c_str());
        return false;
    }
    if (!xbox.Open(x)) {
        printf("cannot open %s\n", x.c_str());
        return false;
    }
    return true;
}

// ---- files

struct Tally {
    int same = 0, differ = 0, gcOnly = 0, xboxOnly = 0;
    double gcBytes = 0, xboxBytes = 0;
};

int Files(const Discs &discs) {
    std::map<std::string, Tally> tallies;   // "kind ext"
    std::set<std::string> seen;            // entries in several archives are counted once
    for (const char *archive : kArchives) {
        GcViv gc, xbox;
        if (!OpenPair(discs, archive, gc, xbox))
            return 1;
        std::string kind = ArchiveKind(archive);
        std::set<std::string> paired;
        for (const GcVivEntry &entry : gc.Entries()) {
            std::string key = kind == "viv" ? Lower(entry.name) : std::string(archive) + "|" + Lower(entry.name);
            const GcVivEntry *twin = xbox.Find(XboxName(entry.name));
            if (twin != NULL)
                paired.insert(Lower(twin->name));
            if (!seen.insert(key).second)
                continue;
            Tally &t = tallies[kind + " " + Extension(entry.name)];
            Bytes a, b;
            gc.Load(entry, a);
            t.gcBytes += (double)a.size();
            if (twin == NULL) {
                t.gcOnly++;
                continue;
            }
            xbox.Load(*twin, b);
            t.xboxBytes += (double)b.size();
            (a == b ? t.same : t.differ)++;
        }
        for (const GcVivEntry &entry : xbox.Entries()) {
            std::string name = Lower(GcName(entry.name));
            std::string key = kind == "viv" ? name : std::string(archive) + "|" + name;
            if (paired.count(Lower(entry.name)) || !seen.insert("xbox|" + key).second)
                continue;
            if (seen.count(key))
                continue;
            Tally &t = tallies[kind + " " + Extension(entry.name)];
            t.xboxOnly++;
            t.xboxBytes += entry.size;
            printf("  Xbox only: %s %s\n", archive, entry.name.c_str());
        }
    }
    printf("%-12s %6s %6s %8s %9s %10s %10s\n", "kind ext", "same", "differ", "GC only", "Xbox only", "GC MB",
           "Xbox MB");
    for (const auto &kv : tallies) {
        const Tally &t = kv.second;
        printf("%-12s %6d %6d %8d %9d %10.2f %10.2f\n", kv.first.c_str(), t.same, t.differ, t.gcOnly, t.xboxOnly,
               t.gcBytes / 1e6, t.xboxBytes / 1e6);
    }
    return 0;
}

// ---- udata

struct Record {
    bool group;
    uint32_t flags, count;
    size_t payload;   // the data record's payload offset (relative records only), else 0
    uint32_t size;
};

// Walks an Xbox-ordered group tree into path -> record
void Walk(const Bytes &data, size_t offset, const std::string &path, std::map<std::string, Record> &out, int depth) {
    if (offset + 16 > data.size() || depth > 64)
        return;
    const uint8_t *r = &data[offset];
    uint32_t tag = Le32(r), flags = Le32(r + 4), items = Le32(r + 8), first = Le32(r + 12);
    char name[16];
    snprintf(name, sizeof(name), "/%c%c%c%c", tag >> 24 & 0xff, tag >> 16 & 0xff, tag >> 8 & 0xff, tag & 0xff);
    std::string here = path + name;
    out[here] = Record{true, flags, items, 0, 0};
    uint32_t groups = flags >> 5;
    size_t base = offset + (size_t)first * 16;
    if (base + ((size_t)groups + items) * 16 > data.size())
        return;
    for (uint32_t i = 0; i < groups; i++)
        Walk(data, base + (size_t)i * 16, here, out, depth + 1);
    for (uint32_t i = 0; i < items; i++) {
        size_t at = base + ((size_t)groups + i) * 16;
        const uint8_t *item = &data[at];
        uint32_t itemTag = Le32(item), itemFlags = Le32(item + 4);
        char itemName[24];
        if (itemFlags & 1)
            snprintf(itemName, sizeof(itemName), "/%c%c#%04x", itemTag >> 24 & 0xff, itemTag >> 16 & 0xff,
                     itemTag & 0xffff);
        else
            snprintf(itemName, sizeof(itemName), "/%c%c%c%c", itemTag >> 24 & 0xff, itemTag >> 16 & 0xff,
                     itemTag >> 8 & 0xff, itemTag & 0xff);
        size_t payload = (itemFlags & 2) ? at + Le32(item + 12) : 0;
        out[here + itemName] = Record{false, itemFlags, Le32(item + 8), payload, itemFlags >> 8};
    }
}

std::string TypeOf(const std::string &path, const Record &r) {
    std::string tag = path.substr(path.find_last_of('/') + 1);
    if (!r.group && (r.flags & 1))
        tag = tag.substr(0, 2) + "##";
    return tag;
}

int UData(const Discs &discs) {
    struct Kind {
        int same = 0, swapped = 0, other = 0, sizeDiffers = 0, absolute = 0;
    };
    std::map<std::string, Kind> kinds;
    int files = 0, failed = 0, records = 0, headerSame = 0, groupsSame = 0, groups = 0, gcOnly = 0, xboxOnly = 0;
    std::map<std::pair<int, int>, int> fields;   // (Xbox 5-bit field, GameCube's)
    std::set<std::string> seen;
    for (const char *archive : kArchives) {
        if (ArchiveKind(archive) != "viv")
            continue;
        GcViv gc, xbox;
        if (!OpenPair(discs, archive, gc, xbox))
            return 1;
        for (const GcVivEntry &entry : gc.Entries()) {
            std::string ext = Extension(entry.name);
            if ((ext != ".crp" && ext != ".gal") || !seen.insert(Lower(entry.name)).second)
                continue;
            const GcVivEntry *twin = xbox.Find(entry.name);
            if (twin == NULL)
                continue;
            Bytes a, b;
            gc.Load(entry, a);
            xbox.Load(*twin, b);
            files++;
            if (!GcUData_ConvertHeaders(a.data(), a.size())) {
                printf("  %s: an offset leaves the file\n", entry.name.c_str());
                failed++;
                continue;
            }
            std::map<std::string, Record> ta, tb;
            Walk(a, 0, "", ta, 0);
            Walk(b, 0, "", tb, 0);
            for (const auto &kv : ta) {
                auto other = tb.find(kv.first);
                if (other == tb.end()) {
                    gcOnly++;
                    continue;
                }
                const Record &ra = kv.second, &rb = other->second;
                if (ra.group) {
                    groups++;
                    if (ra.flags == rb.flags && ra.count == rb.count)
                        groupsSame++;
                    continue;
                }
                records++;
                fields[std::make_pair((int)(rb.flags >> 3 & 0x1f), (int)(ra.flags >> 3 & 0x1f))]++;
                if ((ra.flags & 7) == (rb.flags & 7) && ra.size == rb.size && ra.count == rb.count)
                    headerSame++;
                Kind &k = kinds[TypeOf(kv.first, ra)];
                if (ra.payload == 0 || rb.payload == 0) {
                    k.absolute++;
                    continue;
                }
                if (ra.size != rb.size || ra.payload + ra.size > a.size() || rb.payload + rb.size > b.size()) {
                    k.sizeDiffers++;
                    continue;
                }
                const uint8_t *pa = &a[ra.payload], *pb = &b[rb.payload];
                if (memcmp(pa, pb, ra.size) == 0) {
                    k.same++;
                    continue;
                }
                bool swapped = ra.size % 4 == 0;
                for (uint32_t i = 0; swapped && i < ra.size; i += 4)
                    swapped = Be32(pa + i) == Le32(pb + i);
                (swapped ? k.swapped : k.other)++;
            }
            for (const auto &kv : tb)
                if (!ta.count(kv.first))
                    xboxOnly++;
        }
    }
    printf("%d files (%d failed); groups %d, flags and counts the Xbox's in %d; data records %d, flags, count and size "
           "the Xbox's in %d; records on the GameCube only %d, on the Xbox only %d\n",
           files, failed, groups, groupsSame, records, headerSame, gcOnly, xboxOnly);
    printf("the 5-bit field (Xbox, GameCube):");
    for (const auto &kv : fields)
        printf(" (%d, %d) %d", kv.first.first, kv.first.second, kv.second);
    printf("\n%-8s %8s %8s %8s %8s %8s\n", "type", "same", "swapped", "other", "size", "absolute");
    for (const auto &kv : kinds) {
        const Kind &k = kv.second;
        printf("%-8s %8d %8d %8d %8d %8d\n", kv.first.c_str(), k.same, k.swapped, k.other, k.sizeDiffers, k.absolute);
    }
    return 0;
}

// ---- payloads

// Every paired data record's payload turned round by its type (GcUData_ConvertPayloads) and compared with the Xbox
// twin's, byte for byte, per type; with "detail <type>", where in the type's element the differing bytes fall
int Payloads(const Discs &discs, const std::string &detail) {
    struct Kind {
        int records = 0, identical = 0;
        double bytes = 0, same = 0;
        std::map<uint32_t, int> differ;   // offset in the element -> differing bytes
        uint32_t period = 0;
    };
    std::map<std::string, Kind> kinds;
    std::set<std::string> seen;
    for (const char *archive : kArchives) {
        if (ArchiveKind(archive) != "viv")
            continue;
        GcViv gc, xbox;
        if (!OpenPair(discs, archive, gc, xbox))
            return 1;
        for (const GcVivEntry &entry : gc.Entries()) {
            std::string ext = Extension(entry.name);
            if ((ext != ".crp" && ext != ".gal") || !seen.insert(Lower(entry.name)).second)
                continue;
            const GcVivEntry *twin = xbox.Find(entry.name);
            if (twin == NULL)
                continue;
            Bytes a, b;
            gc.Load(entry, a);
            xbox.Load(*twin, b);
            if (!GcUData_ConvertHeaders(a.data(), a.size()) || !GcUData_ConvertPayloads(a.data(), a.size())) {
                printf("  %s: an offset leaves the file\n", entry.name.c_str());
                continue;
            }
            std::map<std::string, Record> ta, tb;
            Walk(a, 0, "", ta, 0);
            Walk(b, 0, "", tb, 0);
            for (const auto &kv : ta) {
                auto other = tb.find(kv.first);
                const Record &ra = kv.second;
                if (ra.group || other == tb.end() || ra.payload == 0 || other->second.payload == 0)
                    continue;
                const Record &rb = other->second;
                if (ra.size != rb.size || ra.size == 0 || ra.payload + ra.size > a.size() ||
                    rb.payload + rb.size > b.size())
                    continue;
                std::string type = TypeOf(kv.first, ra);
                if (type == "ELFd" || type == "ELFr")
                    continue;
                Kind &k = kinds[type];
                k.records++;
                k.bytes += ra.size;
                uint32_t period = ra.count != 0 && ra.size % ra.count == 0 ? ra.size / ra.count : ra.size;
                int differing = 0;
                for (uint32_t i = 0; i < ra.size; i++) {
                    if (a[ra.payload + i] == b[rb.payload + i])
                        continue;
                    differing++;
                    if (type == detail)
                        k.differ[(i % period) | period << 16]++;
                }
                k.same += ra.size - differing;
                if (type == detail && differing != 0 && k.records - k.identical <= 3) {
                    printf("%s %s (%u bytes, count %u):\n", entry.name.c_str(), kv.first.c_str(), ra.size, ra.count);
                    for (uint32_t i = 0; i < ra.size && i < 0x200; i += 16) {
                        printf("  %04x GC ", i);
                        for (uint32_t j = i; j < i + 16 && j < ra.size; j++)
                            printf("%02x%c", a[ra.payload + j], a[ra.payload + j] != b[rb.payload + j] ? '*' : ' ');
                        printf("\n       XB ");
                        for (uint32_t j = i; j < i + 16 && j < ra.size; j++)
                            printf("%02x ", b[rb.payload + j]);
                        printf("\n");
                    }
                }
                if (differing == 0)
                    k.identical++;
            }
        }
    }
    printf("%-8s %8s %9s %12s %8s\n", "type", "records", "identical", "bytes", "same %");
    for (const auto &kv : kinds) {
        const Kind &k = kv.second;
        printf("%-8s %8d %9d %12.0f %8.3f\n", kv.first.c_str(), k.records, k.identical, k.bytes,
               k.bytes > 0 ? 100.0 * k.same / k.bytes : 100.0);
        for (const auto &d : k.differ)
            printf("    element %u +0x%x: %d bytes differ\n", d.first >> 16, d.first & 0xffff, d.second);
    }
    return 0;
}

// ---- banks

// A bank's patches' PT tags, as (patch, tag, value) for the tags given
struct BankTimbre {
    uint32_t patch, frames, rep, channels;
    uint32_t offsets[6];
    const uint8_t *coefficients;
};

std::vector<BankTimbre> BankTimbres(const Bytes &b, bool big) {
    std::vector<BankTimbre> out;
    auto r16 = [&](size_t at) { return big ? uint32_t(b[at] << 8 | b[at + 1]) : uint32_t(Le16(&b[at])); };
    auto r32 = [&](size_t at) { return big ? Be32(&b[at]) : Le32(&b[at]); };
    static const uint8_t kOffsetTags[6] = { 0x88, 0x89, 0x94, 0x95, 0xa2, 0xa3 };
    uint32_t patches = r16(6);
    for (uint32_t patch = 0; patch < patches && 0x14 + patch * 4 + 4 <= b.size(); patch++) {
        uint32_t offset = r32(0x14 + patch * 4);
        if (offset == 0)
            continue;
        size_t p = 0x14 + patch * 4 + offset + 4;
        BankTimbre t = { patch, 0, 0, 1, {}, NULL };
        while (p < b.size()) {
            uint8_t id = b[p++];
            if (id == 0xff || id == 0xfe) {
                if (t.frames != 0)
                    out.push_back(t);
                t = { patch, 0, 0, 1, {}, NULL };
                if (id == 0xff)
                    break;
                continue;
            }
            if (id >= 0xfc || p >= b.size())
                continue;
            uint32_t length = b[p++], value = 0;
            for (uint32_t i = 0; i < length && i < 4 && p + i < b.size(); i++)
                value = value << 8 | b[p + i];
            if (id == 0x85) t.frames = value;
            if (id == 0xa0) t.rep = value;
            if (id == 0x82) t.channels = value;
            if (id == 0x8f && t.coefficients == NULL) t.coefficients = &b[p];
            for (int ch = 0; ch < 6; ch++)
                if (id == kOffsetTags[ch])
                    t.offsets[ch] = value;
            p += length;
        }
    }
    return out;
}

// Every GameCube bank made the Xbox's (GcSound.cpp): its timbres against the Xbox twin's, and each sample's stand-in
// decoded as the sound outputs decode it (GcPcm_Decode) against the DSP sample's own PCM. GCDRIVE_SHOW=1 prints each
// sample's fingerprint (GcPcm_Fingerprint) by bank and sample number, for tools/gcdriving_check.py's own decode and the
// game's NIGHTFIRE_GC_PCM_SHOW lines.
int Banks(const Discs &discs) {
    const bool show = getenv("GCDRIVE_SHOW") != NULL;
    std::set<std::string> seen;
    std::set<uint32_t> numbers;
    int banks = 0, patchesSame = 0, timbres = 0, timbresTwin = 0, samples = 0, samplesSame = 0;
    double gcSeconds = 0, xboxSeconds = 0, everyBank = 0;
    for (const char *archive : kArchives) {
        if (ArchiveKind(archive) != "viv")
            continue;
        GcViv gc, xbox;
        if (!OpenPair(discs, archive, gc, xbox))
            return 1;
        for (const GcVivEntry &entry : gc.Entries()) {
            if (Extension(entry.name) != ".bnk")
                continue;
            Bytes a, b, converted;
            gc.Load(entry, a);
            uint32_t number = GcPcm_BankNumber(a.data(), uint32_t(a.size()));
            std::vector<GcPcmSample> pcm;
            if (!Gc_ConvertBank(a.data(), uint32_t(a.size()), number, &converted, &pcm)) {
                printf("  %s: not converted\n", entry.name.c_str());
                continue;
            }
            if (numbers.insert(number).second)
                for (const GcPcmSample &sample : pcm)
                    everyBank += sample.pcm.size() * 2.0;
            if (!seen.insert(Lower(entry.name)).second)
                continue;
            banks++;
            std::vector<BankTimbre> source = BankTimbres(a, true), ours = BankTimbres(converted, false);
            const GcVivEntry *twin = xbox.Find(entry.name);
            if (twin != NULL) {
                xbox.Load(*twin, b);
                if (Le16(&b[6]) == Le16(&converted[6]))
                    patchesSame++;
                std::vector<BankTimbre> theirs = BankTimbres(b, false);
                timbresTwin += int(theirs.size());
                for (const BankTimbre &t : theirs)
                    xboxSeconds += t.frames / 16000.0;
            }
            timbres += int(ours.size());
            for (size_t i = 0; i < ours.size() && i < source.size(); i++)
                gcSeconds += source[i].frames / 32000.0;
            // The stand-ins in the order the samples were made: timbre by timbre, channel by channel
            GcPcm_Keep(number, entry.name.c_str(), std::vector<GcPcmSample>(pcm));
            size_t next = 0;
            for (size_t i = 0; i < ours.size() && next < pcm.size(); i++) {
                const BankTimbre &o = ours[i];
                for (uint32_t ch = 0; ch < o.channels && ch < 6 && next < pcm.size(); ch++, next++) {
                    const GcPcmSample &sample = pcm[next];
                    size_t adpcmBytes = (o.frames + 63) / 64 * 36;
                    samples++;
                    if (show)
                        printf("%s %u: %u frames, fingerprint %08x\n", Lower(entry.name).c_str(), unsigned(next),
                               sample.frames, GcPcm_Fingerprint(sample.pcm.data(), sample.frames));
                    if (o.rep != 0x14 || o.frames != sample.frames || o.offsets[ch] + adpcmBytes > converted.size())
                        continue;
                    std::vector<int16_t> back(adpcmBytes / 36 * 64);
                    size_t values = GcPcm_Decode(&converted[o.offsets[ch]], adpcmBytes, 1, back.data(), back.size());
                    if (values == back.size() && values == sample.pcm.size() &&
                        memcmp(back.data(), sample.pcm.data(), values * 2) == 0)
                        samplesSame++;
                }
            }
        }
    }
    printf("%d banks: patch counts the Xbox twin's in %d; timbres %d (the twins %d); %.1f s of samples (the twins "
           "%.1f s at 16 kHz); %d samples, %d decoded from their stand-ins as the PCM they were decoded to; every "
           "bank's PCM %.1f MB\n", banks, patchesSame, timbres, timbresTwin, gcSeconds, xboxSeconds, samples,
           samplesSame, everyBank / 1048576.0);
    return 0;
}

// ---- shapes

// An Xbox SHPX image (EAGL::ShapeImage, eagl/Tar.h) decoded to rows of B, G, R, A: DXT1/3/5 (0x60-0x62), 8-bit with
// its palette attachment (0x7b), 32-bit (0x7d), swizzled by flag 0x2000. False for any other type.
bool DecodeXboxImage(const uint8_t *file, size_t size, uint32_t at, std::vector<uint8_t> *bgra, int *w, int *h) {
    if (at + 16 > size)
        return false;
    uint32_t word = Le32(file + at), type = word & 0xff;
    int width = Le16(file + at + 4), height = Le16(file + at + 6);
    uint32_t flags = Le32(file + at + 12);
    const uint8_t *pixels = file + at + ((flags & 0x1000) ? Le32(file + at + 16) : 16);
    *w = width;
    *h = height;
    bgra->assign(size_t(width) * height * 4, 0);
    auto unswizzle = [&](int x, int y) {
        uint32_t offset = 0, bit = 1;
        for (int sx = 1, sy = 1; sx < width || sy < height; sx <<= 1, sy <<= 1) {
            if (sx < width) {
                if (x & sx)
                    offset |= bit;
                bit <<= 1;
            }
            if (sy < height) {
                if (y & sy)
                    offset |= bit;
                bit <<= 1;
            }
        }
        return offset;
    };
    bool swizzled = (flags & 0x2000) != 0;
    if (type == 0x7d || type == 0x7b) {
        const uint8_t *palette = NULL;
        for (uint32_t a = at, next = word >> 8; next != 0 && a + next + 16 <= size;) {
            a += next;
            uint32_t w2 = Le32(file + a);
            if ((w2 & 0xff) == 0x2a)
                palette = file + a + 16;
            next = w2 >> 8;
        }
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint32_t i = swizzled ? unswizzle(x, y) : uint32_t(y * width + x);
                const uint8_t *c = type == 0x7d ? pixels + i * 4 : palette != NULL ? palette + pixels[i] * 4 : NULL;
                if (c != NULL && c + 4 <= file + size)
                    memcpy(&(*bgra)[(size_t(y) * width + x) * 4], c, 4);
            }
        }
        return true;
    }
    if (type < 0x60 || type > 0x62)
        return false;
    int blockBytes = type == 0x60 ? 8 : 16, bx = (width + 3) / 4;
    for (int by = 0; by < (height + 3) / 4; by++) {
        for (int x0 = 0; x0 < bx; x0++) {
            const uint8_t *block = pixels + (by * bx + x0) * blockBytes;
            if (block + blockBytes > file + size)
                return true;
            const uint8_t *colour = block + blockBytes - 8;
            uint16_t c0 = Le16(colour), c1 = Le16(colour + 2);
            int rgb[4][3];
            for (int k = 0; k < 2; k++) {
                uint16_t c = k == 0 ? c0 : c1;
                rgb[k][2] = (c >> 11) * 255 / 31;
                rgb[k][1] = (c >> 5 & 63) * 255 / 63;
                rgb[k][0] = (c & 31) * 255 / 31;
            }
            bool four = c0 > c1 || type != 0x60;
            for (int k = 0; k < 3; k++) {
                rgb[2][k] = four ? (2 * rgb[0][k] + rgb[1][k]) / 3 : (rgb[0][k] + rgb[1][k]) / 2;
                rgb[3][k] = four ? (rgb[0][k] + 2 * rgb[1][k]) / 3 : 0;
            }
            for (int i = 0; i < 16; i++) {
                int x = x0 * 4 + i % 4, y = by * 4 + i / 4;
                if (x >= width || y >= height)
                    continue;
                int index = colour[4 + i / 4] >> (i % 4 * 2) & 3;
                uint8_t *out = &(*bgra)[(size_t(y) * width + x) * 4];
                for (int k = 0; k < 3; k++)
                    out[k] = uint8_t(rgb[index][k]);
                out[3] = type == 0x60 && !four && index == 3 ? 0 : 255;
            }
        }
    }
    return true;
}

// Every GameCube image container made the Xbox's (GcShape.cpp) and decoded, against the Xbox twin's image of the same
// name where both are decodable and the same size: the mean difference of the colour channels, and the worst images
int Shapes(const Discs &discs) {
    std::set<std::string> seen;
    int files = 0, images = 0, compared = 0, otherSize = 0, undecodable = 0;
    double total = 0;
    std::vector<std::pair<double, std::string>> worst;
    for (const char *archive : kArchives) {
        if (ArchiveKind(archive) != "viv")
            continue;
        GcViv gc, xbox;
        if (!OpenPair(discs, archive, gc, xbox))
            return 1;
        for (const GcVivEntry &entry : gc.Entries()) {
            if (Extension(entry.name) != ".gsh" || !seen.insert(Lower(entry.name)).second)
                continue;
            const GcVivEntry *twin = xbox.Find(XboxName(entry.name));
            Bytes a, b, converted;
            gc.Load(entry, a);
            if (!Gc_ConvertShapes(a.data(), uint32_t(a.size()), &converted))
                continue;
            files++;
            if (twin != NULL)
                xbox.Load(*twin, b);
            uint32_t count = Le32(&converted[8]);
            images += int(count);
            for (uint32_t i = 0; i < count; i++) {
                const uint8_t *e = &converted[16 + i * 8];
                std::vector<uint8_t> ours, theirs;
                int w1, h1, w2, h2;
                if (!DecodeXboxImage(converted.data(), converted.size(), Le32(e + 4), &ours, &w1, &h1)) {
                    undecodable++;
                    continue;
                }
                bool found = false;
                for (uint32_t j = 0; b.size() > 16 && j < Le32(&b[8]) && !found; j++) {
                    if (memcmp(&b[16 + j * 8], e, 4) != 0)
                        continue;
                    found = true;
                    if (!DecodeXboxImage(b.data(), b.size(), Le32(&b[16 + j * 8 + 4]), &theirs, &w2, &h2))
                        break;
                    if (w1 != w2 || h1 != h2) {
                        otherSize++;
                        break;
                    }
                    double sum = 0;
                    for (size_t k = 0; k < ours.size(); k++)
                        if (k % 4 != 3)
                            sum += abs(int(ours[k]) - int(theirs[k]));
                    double mean = sum / (double(w1) * h1 * 3);
                    total += mean;
                    compared++;
                    worst.push_back(std::make_pair(mean, entry.name + " " + std::string((const char *)e, 4)));
                }
            }
        }
    }
    std::sort(worst.rbegin(), worst.rend());
    printf("%d files, %d images: %d compared with the Xbox twin's, mean channel difference %.2f (of 255); %d at "
           "another size, %d not decodable here\n", files, images, compared, compared ? total / compared : 0.0,
           otherSize, undecodable);
    int within[4] = {};   // under 4, 8, 16, 32
    for (const auto &w : worst)
        for (int k = 0; k < 4; k++)
            if (w.first < (4 << k))
                within[k]++;
    printf("  images within 4: %d, 8: %d, 16: %d, 32: %d; the median %.2f\n", within[0], within[1], within[2],
           within[3], worst.empty() ? 0.0 : worst[worst.size() / 2].first);
    for (size_t i = 0; i < worst.size() && i < 12; i++)
        printf("  %6.2f  %s\n", worst[i].first, worst[i].second.c_str());
    return 0;
}

// ---- objects

struct Census {
    std::map<std::string, int> classes, undefined;
    std::map<int, int> relocations;
    int objects = 0, split = 0;
};

// An ELF object (little-endian, as both discs have them); rel is the second half of a split object, or empty
void Survey(const Bytes &dat, const Bytes &rel, Census &c) {
    if (dat.size() < 52 || memcmp(dat.data(), "\x7f" "ELF", 4) != 0)
        return;
    Bytes whole = dat;
    if (!rel.empty() && rel != dat) {
        whole.insert(whole.end(), rel.begin(), rel.end());
        c.split++;
    }
    c.objects++;
    uint32_t shoff = Le32(&whole[0x20]);
    uint16_t shnum = Le16(&whole[0x30]);
    if (shoff + (size_t)shnum * 40 > whole.size())
        return;
    for (int i = 0; i < shnum; i++) {
        const uint8_t *s = &whole[shoff + i * 40];
        uint32_t type = Le32(s + 4), offset = Le32(s + 16), size = Le32(s + 20), link = Le32(s + 24);
        if (offset + (size_t)size > whole.size())
            continue;
        if (type == 9)
            for (uint32_t j = 0; j + 8 <= size; j += 8)
                c.relocations[whole[offset + j + 4]]++;
        if (type != 2 || link >= shnum)
            continue;
        const uint8_t *strtab = &whole[shoff + link * 40];
        uint32_t strings = Le32(strtab + 16);
        for (uint32_t j = 0; j + 16 <= size; j += 16) {
            const uint8_t *sym = &whole[offset + j];
            uint32_t nameAt = strings + Le32(sym);
            if (nameAt >= whole.size())
                continue;
            std::string name((const char *)&whole[nameAt], strnlen((const char *)&whole[nameAt], whole.size() - nameAt));
            size_t sep = name.find(":::");
            if (sep != std::string::npos)
                c.classes[name.substr(0, sep)]++;
            else if (!name.empty() && Le16(sym + 14) == 0)
                c.undefined[name]++;
        }
    }
}

// A .crp's ELFd and ELFr payloads, from its (Xbox-ordered) tree
void CarpObject(const Bytes &file, Bytes &dat, Bytes &rel) {
    std::map<std::string, Record> tree;
    Walk(file, 0, "", tree, 0);
    for (const auto &kv : tree) {
        const Record &r = kv.second;
        if (r.group || r.payload == 0 || r.payload + r.size > file.size())
            continue;
        std::string tag = kv.first.substr(kv.first.find_last_of('/') + 1);
        if (tag == "ELFd")
            dat.assign(file.begin() + r.payload, file.begin() + r.payload + r.size);
        else if (tag == "ELFr")
            rel.assign(file.begin() + r.payload, file.begin() + r.payload + r.size);
    }
}

int Objects(const Discs &discs) {
    Census census[2];
    std::set<std::string> seen[2];
    for (const char *archive : kArchives) {
        if (ArchiveKind(archive) != "viv")
            continue;
        GcViv viv[2];
        if (!OpenPair(discs, archive, viv[0], viv[1]))
            return 1;
        for (int d = 0; d < 2; d++) {
            for (const GcVivEntry &entry : viv[d].Entries()) {
                std::string ext = Extension(entry.name);
                if ((ext != ".o" && ext != ".dat" && ext != ".crp") || !seen[d].insert(Lower(entry.name)).second)
                    continue;
                Bytes dat, rel;
                viv[d].Load(entry, dat);
                if (ext == ".dat") {
                    const GcVivEntry *relEntry = viv[d].Find(entry.name.substr(0, entry.name.size() - 4) + ".rel");
                    if (relEntry != NULL)
                        viv[d].Load(*relEntry, rel);
                } else if (ext == ".crp") {
                    Bytes file = dat;
                    dat.clear();
                    if (d == 0 && !GcUData_ConvertHeaders(file.data(), file.size()))
                        continue;
                    CarpObject(file, dat, rel);
                }
                Survey(dat, rel, census[d]);
            }
        }
    }
    const char *names[2] = {"GameCube", "Xbox"};
    for (int d = 0; d < 2; d++) {
        printf("%s: %d objects, %d split in two (the .rel or ELFr a half of its own); relocation types:", names[d],
               census[d].objects, census[d].split);
        for (const auto &kv : census[d].relocations)
            printf(" %d x%d", kv.first, kv.second);
        printf("\n");
    }
    std::set<std::string> all;
    for (const Census &c : census)
        for (const auto &kv : c.classes)
            all.insert(kv.first);
    printf("%-40s %8s %8s\n", "class", names[0], names[1]);
    for (const std::string &name : all) {
        auto g = census[0].classes.find(name), x = census[1].classes.find(name);
        printf("%-40s %8d %8d\n", name.c_str(), g == census[0].classes.end() ? 0 : g->second,
               x == census[1].classes.end() ? 0 : x->second);
    }
    for (int d = 0; d < 2; d++) {
        printf("%s: names no object defines (%zu):", names[d], census[d].undefined.size());
        for (const auto &kv : census[d].undefined)
            printf(" %s", kv.first.c_str());
        printf("\n");
    }
    return 0;
}

// ---- sound

// An EA "PT" header's tags (each a byte, a length and a big-endian value; 0xfc-0xfe stand alone, 0xff ends)
std::map<int, uint32_t> PtTags(const uint8_t *p, const uint8_t *end, int *platform) {
    std::map<int, uint32_t> tags;
    *platform = -1;
    if (end - p < 4 || p[0] != 'P' || p[1] != 'T')
        return tags;
    *platform = p[2];
    p += 4;
    while (p < end) {
        int tag = *p++;
        if (tag == 0xff)
            break;
        if (tag >= 0xfc)
            continue;
        if (p >= end)
            break;
        int length = *p++;
        uint32_t value = 0;
        for (int i = 0; i < length && p < end; i++)
            value = value << 8 | *p++;
        tags.insert(std::make_pair(tag, value));
    }
    return tags;
}

int Sound(const Discs &discs) {
    // (disc, kind, platform, codec, rate) -> count
    std::map<std::string, int> tally;
    std::set<std::string> seen;
    const char *names[2] = {"GameCube", "Xbox"};
    for (const char *archive : kArchives) {
        GcViv viv[2];
        if (!OpenPair(discs, archive, viv[0], viv[1]))
            return 1;
        for (int d = 0; d < 2; d++) {
            for (const GcVivEntry &entry : viv[d].Entries()) {
                std::string ext = Extension(entry.name);
                std::string key = std::string(names[d]) + archive + Lower(entry.name);
                if ((ext != ".asf" && ext != ".bnk") || !seen.insert(key).second)
                    continue;
                Bytes b;
                viv[d].Load(entry, b);
                std::vector<size_t> headers;
                if (ext == ".asf" && b.size() > 8 && memcmp(b.data(), "SCHl", 4) == 0) {
                    headers.push_back(8);
                } else if (ext == ".bnk" && b.size() > 16 && memcmp(b.data(), "BNK", 3) == 0) {
                    bool big = b[3] == 'b';
                    uint16_t patches = big ? (uint16_t)(b[6] << 8 | b[7]) : Le16(&b[6]);
                    for (uint32_t i = 0; i < patches && 16 + i * 4 + 4 <= b.size(); i++) {
                        uint32_t at = big ? Be32(&b[16 + i * 4]) : Le32(&b[16 + i * 4]);
                        if (at != 0)
                            headers.push_back(16 + i * 4 + at);
                    }
                }
                for (size_t at : headers) {
                    if (at >= b.size())
                        continue;
                    int platform;
                    std::map<int, uint32_t> tags = PtTags(&b[at], b.data() + b.size(), &platform);
                    // rates to the nearest kHz: the Xbox's banks have many a few Hz apart
                    uint32_t rate = tags.count(0x84) ? tags[0x84] : 0;
                    char line[128];
                    snprintf(line, sizeof(line), "%-8s %s platform %d codec %u rate ~%u kHz", names[d],
                             ext == ".asf" ? (strstr(archive, ".mus") ? "music stream" : "speech stream")
                                           : "bank patch  ",
                             platform, tags.count(0xa0) ? tags[0xa0] : 0, (rate + 500) / 1000);
                    tally[line]++;
                }
            }
        }
    }
    for (const auto &kv : tally)
        printf("%6d  %s\n", kv.second, kv.first.c_str());
    return 0;
}

}   // namespace

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("gcdrive_check <gc disc> <xbox disc> files|udata|objects|sound\n");
        return 1;
    }
    Discs discs = {argv[1], argv[2]};
    std::string command = argv[3];
    if (command == "files")
        return Files(discs);
    if (command == "udata")
        return UData(discs);
    if (command == "banks")
        return Banks(discs);
    if (command == "shapes")
        return Shapes(discs);
    if (command == "payloads")
        return Payloads(discs, argc > 4 ? argv[4] : "");
    if (command == "objects")
        return Objects(discs);
    if (command == "sound")
        return Sound(discs);
    printf("unknown command %s\n", command.c_str());
    return 1;
}
