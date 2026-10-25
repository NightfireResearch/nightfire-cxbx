#ifndef LAUNCHER_ZIP_H_
#define LAUNCHER_ZIP_H_

// A release zip unpacked into a folder (miniz). Entries are refused, and nothing more written, if one would land
// outside the folder (an absolute path, a drive, "..").

#include <stdint.h>

#include <filesystem>
#include <functional>
#include <string>

bool Zip_Extract(const std::filesystem::path &zip, const std::filesystem::path &folder, std::string *error,
                 const std::function<void(uint64_t done, uint64_t total)> &progress = nullptr);

#endif // LAUNCHER_ZIP_H_
