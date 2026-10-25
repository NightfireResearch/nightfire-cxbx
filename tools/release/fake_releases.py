#!/usr/bin/env python3
"""A fake GitHub releases API on disk, signed with the test key, for the launcher's tests (launcher --selftest).

    python tools/release/fake_releases.py --build build/windows/Release --out FOLDER [--base-url URL] [--serve PORT]

FOLDER then holds, for each scenario, what api.github.com and the release downloads would answer:

    repos/selftest/<scenario>/releases/latest        the release (GitHub's /releases/latest)
    repos/selftest/<scenario>/releases/index.html    the releases list (/releases, served for the folder)
    download/<scenario>/...                          manifest.json, manifest.json.sig, the zip
    bundle/                                          a release zip extracted by hand: the first run's source

with the scenarios
    good          90.0.0, all in order
    badsig        90.0.0, the manifest changed after it was signed
    badhash       90.0.0, a byte of the zip changed after the manifest was written
    minlauncher   90.1.0, minLauncher 90.1.0: the launcher replaces itself first, and the new one installs it
and bundle/ at 89.0.0. Every version's files are the one build's.

--base-url is where FOLDER is reached: file:///C:/... (the default, FOLDER as a file URL) or http://127.0.0.1:PORT
with --serve PORT, which then serves FOLDER on 127.0.0.1 until stopped (the launcher's WinHTTP path, redirects
included: /releases redirects to /releases/). The launcher is pointed at it with --api-base BASE-URL and
--repository selftest/<scenario>.
"""

import argparse
import functools
import http.server
import json
import os
import pathlib
import shutil
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ed25519  # noqa: E402
import package  # noqa: E402
import sign_manifest  # noqa: E402

PLATFORM = "windows-x86"
BUNDLE_VERSION = "89.0.0"


def release_json(base_url, scenario, version, assets, minimal=False):
    return {
        "tag_name": "v" + version,
        "name": f"Nightfire {version} ({scenario}, a test release)",
        "body": f"A fake release for the launcher's tests: the {scenario} scenario.\n\n- Signed with the TEST key.",
        "html_url": f"{base_url}/download/{scenario}/",
        "draft": False,
        "prerelease": False,
        "assets": [{"name": name, "size": os.path.getsize(path),
                    "browser_download_url": f"{base_url}/download/{scenario}/{name}"} for name, path in assets],
    }


def make_scenario(out, base_url, build, sdl_licence, scenario, version, min_launcher):
    folder = os.path.join(out, "download", scenario)
    os.makedirs(folder, exist_ok=True)
    zip_path = package.write_zip(PLATFORM, version, build, folder, sdl_licence)
    data = package.manifest(version, [zip_path], min_launcher=min_launcher, repository="selftest/" + scenario)
    manifest_path = os.path.join(folder, "manifest.json")
    package.write_manifest(manifest_path, data)
    seed = sign_manifest.read_key_file(sign_manifest.TEST_KEY_FILE)
    signature = ed25519.sign(seed, open(manifest_path, "rb").read())
    with open(manifest_path + ".sig", "w", newline="\n") as f:
        f.write(signature.hex() + "\n")

    if scenario == "badsig":   # signed, then changed
        data["date"] = "1999-01-01"
        package.write_manifest(manifest_path, data)
    if scenario == "badhash":   # one byte of the zip changed, its size kept
        with open(zip_path, "r+b") as f:
            f.seek(100)
            byte = f.read(1)
            f.seek(100)
            f.write(bytes([byte[0] ^ 0xFF]))

    assets = [(os.path.basename(p), p) for p in (manifest_path, manifest_path + ".sig", zip_path)]
    release = release_json(base_url, scenario, version, assets)
    api = os.path.join(out, "repos", "selftest", scenario, "releases")
    os.makedirs(api, exist_ok=True)
    with open(os.path.join(api, "latest"), "w", newline="\n") as f:
        json.dump(release, f, indent=2)
    with open(os.path.join(api, "index.html"), "w", newline="\n") as f:
        json.dump([release], f, indent=2)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", required=True, help="the build folder (action_standalone.exe, launcher.exe...)")
    parser.add_argument("--out", required=True)
    parser.add_argument("--base-url", help="where --out is reached (default: its file:// URL)")
    parser.add_argument("--serve", type=int, help="serve --out on 127.0.0.1:PORT afterwards")
    parser.add_argument("--sdl-licence")
    args = parser.parse_args()

    out = os.path.abspath(args.out)
    if os.path.exists(out):
        if not os.path.exists(os.path.join(out, "fake-releases.txt")):
            sys.exit(f"{out} exists and is not a fake releases folder: not touching it")
        shutil.rmtree(out)
    os.makedirs(out)
    with open(os.path.join(out, "fake-releases.txt"), "w") as f:
        f.write("Made by tools/release/fake_releases.py: test releases signed with the TEST key.\n")
    base_url = args.base_url or (f"http://127.0.0.1:{args.serve}" if args.serve else pathlib.Path(out).as_uri())
    base_url = base_url.rstrip("/")

    # minLauncher 0.0.0 lets the test's launcher (0.0.0+<commit>, untagged) install the first three itself
    for scenario, version, min_launcher in (("good", "90.0.0", "0.0.0"),
                                            ("badsig", "90.0.0", "0.0.0"),
                                            ("badhash", "90.0.0", "0.0.0"),
                                            ("minlauncher", "90.1.0", "90.1.0")):
        make_scenario(out, base_url, args.build, args.sdl_licence, scenario, version, min_launcher)

    bundle_zip = package.write_zip(PLATFORM, BUNDLE_VERSION, args.build, os.path.join(out, "bundle-zip"),
                                   args.sdl_licence)
    with zipfile.ZipFile(bundle_zip) as z:
        z.extractall(os.path.join(out, "bundle"))
    print(f"fake releases in {out}, reached as {base_url}: --api-base {base_url} --repository selftest/good")

    if args.serve:
        class Server(http.server.ThreadingHTTPServer):
            allow_reuse_address = False   # on Windows it would share a port another program is listening on

        handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=out)
        server = Server(("127.0.0.1", args.serve), handler)
        print(f"serving on 127.0.0.1:{args.serve}", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
