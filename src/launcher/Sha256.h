#ifndef LAUNCHER_SHA256_H_
#define LAUNCHER_SHA256_H_

// SHA-256 (FIPS 180-4), for the release zips' and the disc files' checks.

#include <stddef.h>
#include <stdint.h>

#include <string>

class Sha256 {
public:
    Sha256();
    void Update(const void *data, size_t size);
    void Final(uint8_t digest[32]);
    std::string FinalHex();   // lowercase hex, as manifest.json and the release table hold it

private:
    void Block(const uint8_t *block);

    uint32_t m_state[8];
    uint64_t m_length = 0;
    uint8_t m_buffer[64];
    size_t m_buffered = 0;
};

#endif // LAUNCHER_SHA256_H_
