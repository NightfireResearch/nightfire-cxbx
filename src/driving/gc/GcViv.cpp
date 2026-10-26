#include "GcViv.h"

#include "../platform/RefPack.h"

#include <ctype.h>
#include <string.h>

static uint32_t Be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static bool SameName(const std::string &a, const std::string &b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
    return true;
}

bool GcViv::Open(const std::string &path) {
    Close();
    file = fopen(path.c_str(), "rb");
    if (file == NULL)
        return false;
    uint8_t header[16];
    if (fread(header, 1, 16, file) != 16 || memcmp(header, "BIGF", 4) != 0) {
        Close();
        return false;
    }
    uint32_t count = Be32(header + 8);
    uint32_t dirSize = Be32(header + 12);
    if (dirSize < 16) {
        Close();
        return false;
    }
    std::vector<uint8_t> dir(dirSize);
    memcpy(dir.data(), header, 16);
    if (fread(dir.data() + 16, 1, dirSize - 16, file) != dirSize - 16) {
        Close();
        return false;
    }
    size_t p = 16;
    for (uint32_t i = 0; i < count && p + 9 <= dir.size(); i++) {
        GcVivEntry entry;
        entry.offset = Be32(&dir[p]);
        entry.size = Be32(&dir[p + 4]);
        p += 8;
        size_t end = p;
        while (end < dir.size() && dir[end] != 0)
            end++;
        entry.name.assign((const char *)&dir[p], end - p);
        p = end + 1;
        entries.push_back(entry);
    }
    return true;
}

void GcViv::Close() {
    if (file != NULL)
        fclose(file);
    file = NULL;
    entries.clear();
}

const GcVivEntry *GcViv::Find(const std::string &name) const {
    for (const GcVivEntry &entry : entries)
        if (SameName(entry.name, name))
            return &entry;
    return NULL;
}

bool GcViv::Read(const GcVivEntry &entry, std::vector<uint8_t> &out) const {
    out.resize(entry.size);
    if (file == NULL || fseek(file, (long)entry.offset, SEEK_SET) != 0)
        return false;
    return fread(out.data(), 1, entry.size, file) == entry.size;
}

bool GcViv::Load(const GcVivEntry &entry, std::vector<uint8_t> &out) const {
    if (!Read(entry, out))
        return false;
    GcViv_Unpack(out);
    return true;
}

bool GcViv_Unpack(std::vector<uint8_t> &data) {
    if (data.size() < 6 || data[1] != 0xfb || ((data[0] & 0xfe) != 0x10 && (data[0] & 0xfe) != 0x90))
        return false;
    std::vector<uint8_t> out(unpacksizez(data.data()));
    int size = UNPACK_unpack(data.data(), out.data());
    if (size <= 0)
        return false;
    out.resize((size_t)size);
    data.swap(out);
    return true;
}
