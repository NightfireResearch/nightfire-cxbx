#ifndef LAUNCHER_RELEASEKEY_H_
#define LAUNCHER_RELEASEKEY_H_

// The Ed25519 public keys a release's manifest.json.sig is checked against (docs/install-and-update.md, "The release
// key").
//
// kReleasePublicKey is the project's: 64 hex digits from `python tools/release/sign_manifest.py keygen`, whose private
// half is the repository secret NIGHTFIRE_RELEASE_KEY. Empty until the key is made: such a launcher installs no
// update, and a release build (NIGHTFIRE_RELEASE=ON) refuses to configure. The release job also checks that the
// secret signs for this key before it publishes anything.
static const char kReleasePublicKey[] = "";

// tools/release/test_key.txt's public half: trusted only by a launcher built with NIGHTFIRE_LAUNCHER_TEST_KEY=ON,
// for the local tests (--selftest, tools/release/fake_releases.py). A release build refuses that option.
static const char kTestPublicKey[] = "36d671bf247b437d38fa3bd105d8ade4a86e8de379c61d28007aa66ca14ba8d1";

#endif // LAUNCHER_RELEASEKEY_H_
