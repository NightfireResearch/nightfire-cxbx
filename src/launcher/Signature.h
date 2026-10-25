#ifndef LAUNCHER_SIGNATURE_H_
#define LAUNCHER_SIGNATURE_H_

// The manifest's signature check: Ed25519 (Monocypher's crypto_ed25519_check) against the keys in ReleaseKey.h.
// The signature file is 128 hex digits, as tools/release/sign_manifest.py writes it.

#include <string>

// Whether this launcher has a key to check releases with at all
bool Signature_HaveKey();

// Whether this build trusts the test key (NIGHTFIRE_LAUNCHER_TEST_KEY): the window says so in red
bool Signature_TestKeyBuild();

// True if signatureText signs data's exact bytes under a trusted key; otherwise false, and why in error
bool Signature_Verify(const std::string &data, const std::string &signatureText, std::string *error);

// Hex text to bytes; false unless it is exactly size bytes' worth (surrounding white space is allowed)
bool Signature_ParseHex(const std::string &text, unsigned char *out, size_t size);

#endif // LAUNCHER_SIGNATURE_H_
