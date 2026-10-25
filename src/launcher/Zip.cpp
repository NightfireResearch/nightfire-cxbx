#include "Zip.h"

#include <fstream>
#include <string.h>

#include "Platform.h"
#include "miniz.h"

namespace {

// miniz opens files with fopen, which on Windows cannot name a path outside the ANSI code page; it reads through
// this instead
size_t ReadAt(void *opaque, mz_uint64 offset, void *buffer, size_t size) {
    std::ifstream &in = *static_cast<std::ifstream *>(opaque);
    in.clear();
    in.seekg(std::streamoff(offset));
    in.read(static_cast<char *>(buffer), std::streamsize(size));
    return size_t(in.gcount());
}

size_t WriteTo(void *opaque, mz_uint64, const void *buffer, size_t size) {
    std::ofstream &out = *static_cast<std::ofstream *>(opaque);
    out.write(static_cast<const char *>(buffer), std::streamsize(size));
    return out ? size : 0;
}

// A relative path of plain names: no root, no drive, no "." or ".." parts
bool SafeEntryName(const std::string &name) {
    if (name.empty() || name[0] == '/' || name[0] == '\\' || name.find(':') != std::string::npos)
        return false;
    size_t start = 0;
    while (start <= name.size()) {
        size_t end = name.find_first_of("/\\", start);
        std::string part = name.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "." || part == "..")
            return false;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return true;
}

} // namespace

bool Zip_Extract(const fs::path &zip, const fs::path &folder, std::string *error,
                 const std::function<void(uint64_t, uint64_t)> &progress) {
    std::ifstream in(zip, std::ios::binary);
    std::error_code sizeError;
    uint64_t zipSize = fs::file_size(zip, sizeError);
    if (!in || sizeError) {
        *error = "cannot read " + ToUtf8(zip);
        return false;
    }
    mz_zip_archive archive;
    memset(&archive, 0, sizeof(archive));
    archive.m_pRead = ReadAt;
    archive.m_pIO_opaque = &in;
    if (!mz_zip_reader_init(&archive, zipSize, 0)) {
        *error = "not a zip file: " + ToUtf8(zip);
        return false;
    }

    bool ok = true;
    mz_uint count = mz_zip_reader_get_num_files(&archive);
    uint64_t total = 0, done = 0;
    for (mz_uint i = 0; i < count; i++) {
        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&archive, i, &stat))
            total += stat.m_uncomp_size;
    }
    for (mz_uint i = 0; i < count && ok; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&archive, i, &stat)) {
            *error = "a damaged entry in " + ToUtf8(zip);
            ok = false;
            break;
        }
        std::string name = stat.m_filename;
        if (!SafeEntryName(name)) {
            *error = "refusing an entry outside the folder: " + name;
            ok = false;
            break;
        }
        fs::path target = folder / FromUtf8(name);
        std::error_code ec;
        if (stat.m_is_directory) {
            fs::create_directories(target, ec);
            continue;
        }
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out || !mz_zip_reader_extract_to_callback(&archive, i, WriteTo, &out, 0)) {
            *error = "cannot unpack " + name + " (" + mz_zip_get_error_string(mz_zip_get_last_error(&archive)) + ")";
            ok = false;
            break;
        }
        done += stat.m_uncomp_size;
        if (progress)
            progress(done, total);
    }
    mz_zip_reader_end(&archive);
    return ok;
}
