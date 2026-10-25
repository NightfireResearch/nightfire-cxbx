#!/usr/bin/env python3
"""Signs a release's manifest.json with the project's Ed25519 key, and checks such a signature.

    python tools/release/sign_manifest.py keygen [--private-out FILE]
        a new key pair: the public key to put in src/launcher/ReleaseKey.h, the private key (64 hex digits) to keep
        as the repository secret NIGHTFIRE_RELEASE_KEY (docs/install-and-update.md, "The release key")
    python tools/release/sign_manifest.py public (--key-env NAME | --key-file FILE | --test-key)
        the public key of a private key
    python tools/release/sign_manifest.py sign MANIFEST (--key-env NAME | --key-file FILE | --test-key) [--out SIG]
        writes MANIFEST.sig: the signature of the file's exact bytes, as 128 hex digits and a newline
    python tools/release/sign_manifest.py verify MANIFEST [SIG] (--public HEX | --header FILE | --test-key)
        checks it; --header reads the key the launcher is built with (src/launcher/ReleaseKey.h)

--test-key is tools/release/test_key.txt, a key for local tests only: its public half is built into a launcher only
with NIGHTFIRE_LAUNCHER_TEST_KEY=ON, which a release build refuses (CMakeLists.txt), and "sign" refuses it with
--release, as the release job runs it.
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ed25519  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
TEST_KEY_FILE = os.path.join(HERE, "test_key.txt")


def parse_hex_key(text, what):
    text = text.strip()
    if not re.fullmatch(r"[0-9a-fA-F]{64}", text):
        sys.exit(f"{what}: expected 64 hex digits (a 32-byte Ed25519 key)")
    return bytes.fromhex(text)


def read_key_file(path):
    # The last non-comment line holds the key, so that the test key's file can say what it is above it
    lines = [line.strip() for line in open(path, encoding="utf-8") if line.strip() and not line.startswith("#")]
    if not lines:
        sys.exit(f"{path}: no key")
    return parse_hex_key(lines[-1], path)


def private_key(args):
    if getattr(args, "test_key", False):
        return read_key_file(TEST_KEY_FILE)
    if args.key_env:
        value = os.environ.get(args.key_env, "")
        if not value:
            sys.exit(f"the environment variable {args.key_env} is empty: is the repository secret set?")
        return parse_hex_key(value, args.key_env)
    if args.key_file:
        return read_key_file(args.key_file)
    sys.exit("which key: --key-env NAME, --key-file FILE or --test-key")


def header_key(path):
    text = open(path, encoding="utf-8").read()
    m = re.search(r'kReleasePublicKey\[\]\s*=\s*"([0-9a-fA-F]*)"', text)
    if not m:
        sys.exit(f"{path}: no kReleasePublicKey")
    if not m.group(1):
        sys.exit(f"{path}: kReleasePublicKey is empty - generate the release key first (keygen)")
    return parse_hex_key(m.group(1), path)


def test_public():
    return ed25519.public_key(read_key_file(TEST_KEY_FILE))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    keygen = sub.add_parser("keygen", help="a new key pair")
    keygen.add_argument("--private-out", help="write the private key to this file instead of printing it")

    def key_options(p):
        p.add_argument("--key-env", help="the private key's environment variable (CI: the secret)")
        p.add_argument("--key-file", help="a file holding the private key")
        p.add_argument("--test-key", action="store_true", help="tools/release/test_key.txt (local tests only)")

    public = sub.add_parser("public", help="the public key of a private key")
    key_options(public)

    sign = sub.add_parser("sign", help="sign a manifest")
    sign.add_argument("manifest")
    sign.add_argument("--out", help="the signature file (default MANIFEST.sig)")
    sign.add_argument("--release", action="store_true", help="refuse the test key")
    key_options(sign)

    verify = sub.add_parser("verify", help="check a manifest's signature")
    verify.add_argument("manifest")
    verify.add_argument("signature", nargs="?")
    verify.add_argument("--public", help="the public key, 64 hex digits")
    verify.add_argument("--header", help="read the public key from the launcher's src/launcher/ReleaseKey.h")
    verify.add_argument("--test-key", action="store_true")

    args = parser.parse_args()

    if args.command == "keygen":
        seed = ed25519.generate_seed()
        print("public key (src/launcher/ReleaseKey.h, kReleasePublicKey):")
        print("  " + ed25519.public_key(seed).hex())
        if args.private_out:
            with open(args.private_out, "x", encoding="utf-8") as f:
                f.write(seed.hex() + "\n")
            print(f"private key written to {args.private_out}: store it as the repository secret "
                  "NIGHTFIRE_RELEASE_KEY, keep a copy offline, then delete the file")
        else:
            print("private key (the repository secret NIGHTFIRE_RELEASE_KEY; never commit it):")
            print("  " + seed.hex())
        return

    if args.command == "public":
        print(ed25519.public_key(private_key(args)).hex())
        return

    if args.command == "sign":
        seed = private_key(args)
        if ed25519.public_key(seed) == test_public():
            if args.release:
                sys.exit("refusing to sign a release with the test key (tools/release/test_key.txt)")
            print("WARNING: signing with the TEST key - for local tests only", file=sys.stderr)
        data = open(args.manifest, "rb").read()
        signature = ed25519.sign(seed, data)
        out = args.out or args.manifest + ".sig"
        with open(out, "w", encoding="ascii", newline="\n") as f:
            f.write(signature.hex() + "\n")
        print(f"{out}: signed by {ed25519.public_key(seed).hex()}")
        return

    if args.command == "verify":
        if args.public:
            public_bytes = parse_hex_key(args.public, "--public")
        elif args.header:
            public_bytes = header_key(args.header)
        elif args.test_key:
            public_bytes = test_public()
        else:
            sys.exit("which key: --public HEX, --header FILE or --test-key")
        data = open(args.manifest, "rb").read()
        signature_text = open(args.signature or args.manifest + ".sig", encoding="ascii").read().strip()
        if not re.fullmatch(r"[0-9a-fA-F]{128}", signature_text):
            sys.exit("the signature file is not 128 hex digits")
        if not ed25519.verify(public_bytes, data, bytes.fromhex(signature_text)):
            sys.exit(f"BAD signature for {args.manifest} (key {public_bytes.hex()})")
        print(f"{args.manifest}: good signature (key {public_bytes.hex()})")


if __name__ == "__main__":
    main()
