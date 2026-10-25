#include "Signature.h"

#include <string.h>

#include "ReleaseKey.h"
#include "monocypher-ed25519.h"

namespace {

int HexDigit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool CheckWith(const char *keyHex, const std::string &data, const unsigned char signature[64]) {
    unsigned char key[32];
    if (!Signature_ParseHex(keyHex, key, sizeof(key)))
        return false;
    return crypto_ed25519_check(signature, key, reinterpret_cast<const unsigned char *>(data.data()), data.size()) == 0;
}

} // namespace

bool Signature_ParseHex(const std::string &text, unsigned char *out, size_t size) {
    size_t start = text.find_first_not_of(" \t\r\n");
    size_t end = text.find_last_not_of(" \t\r\n");
    if (start == std::string::npos || end - start + 1 != size * 2)
        return false;
    for (size_t i = 0; i < size; i++) {
        int high = HexDigit(text[start + i * 2]), low = HexDigit(text[start + i * 2 + 1]);
        if (high < 0 || low < 0)
            return false;
        out[i] = static_cast<unsigned char>(high << 4 | low);
    }
    return true;
}

bool Signature_TestKeyBuild() {
#ifdef NIGHTFIRE_LAUNCHER_TEST_KEY
    return true;
#else
    return false;
#endif
}

bool Signature_HaveKey() {
    return kReleasePublicKey[0] != '\0' || Signature_TestKeyBuild();
}

bool Signature_Verify(const std::string &data, const std::string &signatureText, std::string *error) {
    if (!Signature_HaveKey()) {
        *error = "this launcher was built without the release key, so it cannot check updates";
        return false;
    }
    unsigned char signature[64];
    if (!Signature_ParseHex(signatureText, signature, sizeof(signature))) {
        *error = "the signature file is not a signature (128 hex digits)";
        return false;
    }
    if (kReleasePublicKey[0] != '\0' && CheckWith(kReleasePublicKey, data, signature))
        return true;
    if (Signature_TestKeyBuild() && CheckWith(kTestPublicKey, data, signature))
        return true;
    *error = "the manifest's signature is not valid: it was not signed by this project's release key";
    return false;
}
