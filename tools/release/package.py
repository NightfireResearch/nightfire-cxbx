#!/usr/bin/env python3
"""A release's files: each platform's zip, and the manifest that lists them (docs/install-and-update.md).

    python tools/release/package.py zip --platform windows-x86 --version 0.4.0 --build build/windows/Release --out dist
        dist/nightfire-windows-x86-0.4.0.zip: the standalone executables, the launcher, FFmpeg's libraries and the
        licences, with nightfire-release.json (what the launcher reads when it is run from the extracted zip)
    python tools/release/package.py manifest --version 0.4.0 --out dist/manifest.json dist/nightfire-*.zip
        manifest.json: the version, the date, minLauncher, the repository, and each zip's name, size and SHA-256.
        Then sign it: tools/release/sign_manifest.py sign dist/manifest.json

The manifest's format only ever gains fields: an older launcher ignores what it does not know. Raise MIN_LAUNCHER
when a release needs a newer launcher than the last one to install it (a change in how releases are laid out).
"""

import argparse
import datetime
import glob
import hashlib
import json
import os
import re
import sys
import zipfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REPOSITORY = "NightfireResearch/nightfire-cxbx"
MIN_LAUNCHER = "0.1.0"
FORMAT = 1

# Each platform's files, as globs in the build folder; every one must match something. TODO: windows-x64,
# linux-x86_64 and macos-arm64 (the design's step 7), each when it is ready for players.
PLATFORMS = {
    "windows-x86": [
        "action_standalone.exe",
        "driving_standalone.exe",
        "launcher.exe",
        "avcodec-*.dll",
        "avformat-*.dll",
        "avutil-*.dll",
        "FFmpeg-*.txt",
    ],
}

# The third-party licences, as licences/<name>
LICENCES = {
    "imgui.txt": "third_party/imgui/LICENSE.txt",
    "miniz.txt": "third_party/miniz/LICENSE",
    "monocypher.md": "third_party/monocypher/LICENCE.md",
}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def check_version(version):
    if not re.fullmatch(r"\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?", version):
        sys.exit(f"{version}: a release version is MAJOR.MINOR.PATCH, with an optional -pre-release")


def zip_name(platform, version):
    return f"nightfire-{platform}-{version}.zip"


def collect(platform, build, sdl_licence=None):
    """(source, name in the zip) for each of a platform's files"""
    if platform not in PLATFORMS:
        sys.exit(f"{platform}: not a release platform yet ({', '.join(PLATFORMS)})")
    files = []
    for pattern in PLATFORMS[platform]:
        matches = sorted(glob.glob(os.path.join(build, pattern)))
        if not matches:
            sys.exit(f"{build}: nothing matches {pattern}")
        files += [(m, os.path.basename(m)) for m in matches]
    for name, source in LICENCES.items():
        files.append((os.path.join(REPO, source), "licences/" + name))
    if sdl_licence is None:   # SDL's source, fetched by CMake beside the build
        found = glob.glob(os.path.join(build, "..", "_deps", "sdl3-src", "LICENSE.txt")) + \
            glob.glob(os.path.join(build, "_deps", "sdl3-src", "LICENSE.txt"))
        sdl_licence = found[0] if found else None
    if sdl_licence is None:
        sys.exit("SDL's LICENSE.txt was not found beside the build: give it with --sdl-licence")
    files.append((sdl_licence, "licences/SDL3.txt"))
    return files


def write_zip(platform, version, build, out, sdl_licence=None, min_launcher=MIN_LAUNCHER):
    check_version(version)
    files = collect(platform, build, sdl_licence)
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, zip_name(platform, version))
    release = {
        "format": FORMAT,
        "version": version,
        "platform": platform,
        "minLauncher": min_launcher,
        "files": [name for _, name in files] + ["nightfire-release.json"],
    }
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for source, name in files:
            z.write(source, name)
        z.writestr("nightfire-release.json", json.dumps(release, indent=2) + "\n")
    return path


def manifest(version, zips, date=None, min_launcher=MIN_LAUNCHER, repository=REPOSITORY):
    check_version(version)
    platforms = {}
    for path in zips:
        name = os.path.basename(path)
        m = re.fullmatch(r"nightfire-(.+)-" + re.escape(version) + r"\.zip", name)
        if not m:
            sys.exit(f"{name}: not nightfire-<platform>-{version}.zip")
        platforms[m.group(1)] = {"zip": name, "size": os.path.getsize(path), "sha256": sha256(path)}
    return {
        "format": FORMAT,
        "version": version,
        "date": date or datetime.date.today().isoformat(),
        "minLauncher": min_launcher,
        "repository": repository,
        "platforms": platforms,
    }


def write_manifest(path, data):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(data, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    z = sub.add_parser("zip")
    z.add_argument("--platform", required=True)
    z.add_argument("--version", required=True)
    z.add_argument("--build", required=True, help="the folder the build put the executables in")
    z.add_argument("--out", required=True)
    z.add_argument("--sdl-licence", help="SDL's LICENSE.txt (default: CMake's fetched copy beside the build)")
    m = sub.add_parser("manifest")
    m.add_argument("--version", required=True)
    m.add_argument("--out", required=True)
    m.add_argument("--date", help="YYYY-MM-DD (default today)")
    m.add_argument("--min-launcher", default=MIN_LAUNCHER)
    m.add_argument("--repository", default=REPOSITORY)
    m.add_argument("zips", nargs="+")
    args = parser.parse_args()

    if args.command == "zip":
        path = write_zip(args.platform, args.version, args.build, args.out, args.sdl_licence)
        print(f"{path}: {os.path.getsize(path):,} bytes")
    else:
        data = manifest(args.version, args.zips, args.date, args.min_launcher, args.repository)
        write_manifest(args.out, data)
        print(f"{args.out}: {', '.join(data['platforms'])}")


if __name__ == "__main__":
    main()
