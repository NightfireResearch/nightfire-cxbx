# Installing and updating

How someone who is not a developer could get this project onto their computer, point it at their own copy of
the game, and keep it up to date. The design dates from 24 October 2026. Steps 1-6 of its order of work (below)
were built for Windows x86 on 25 October 2026: the versioned executables (`cmake/version.cmake`), the release job
(`.github/workflows/release.yml`, `tools/release/`) and the launcher (`src/launcher/`, the CMake option
`NIGHTFIRE_LAUNCHER`). "Where it stands", at the end, lists what is built, how it is tested and what is left.

## What a player has to end up with

Since October 2026 both engines run as standalone programs: `action_standalone` (on foot) and
`driving_standalone` (the car missions). Neither needs the XBEs or an emulator; when the story moves between on
foot and driving, each starts the other from its own folder (`src/common/launchInfo.cpp`). What a working setup
needs:

| | Where it is read from today |
|---|---|
| The two executables and their libraries (FFmpeg; SDL3 on Linux and macOS) | together in one folder |
| The game's data from the player's own Xbox disc: `eurocom/` (1.7 GB) and `driving/` (1.5 GB) | `DiscPath` in `settings.ini`: absolute, or relative to the executables' folder (`src/common/xboxPath.cpp`) |
| `settings.ini`, the saves (`saves/`, `tdata/`, `cache/`), logs | the working directory |

That split already suits an installer. The program files can be replaced without touching anything of the
player's: the launcher starts the game with the working directory set to the player's own folder, and that
folder's `settings.ini` gives an absolute `DiscPath`. **No change to the game is needed for any of this.**

We never distribute, download or upload game data. The player supplies their own disc.

## Folder layout

One folder per user, writable without administrator rights:

| | Windows | Linux | macOS |
|---|---|---|---|
| root | `%LOCALAPPDATA%\Nightfire\` | `$XDG_DATA_HOME/nightfire/` (`~/.local/share/nightfire/`) | `~/Library/Application Support/Nightfire/` |

```
Nightfire/
  launcher(.exe)          the launcher (also in the release; updates itself, below)
  launcher.json           installed version, update channel, the folders below
  app/
    0.4.0/                one release: action_standalone, driving_standalone, libraries
    0.3.2/                the previous one, kept so an update can be undone
  disc/                   the player's game data, copied in by first-time setup
    eurocom/  driving/
  user/                   the working directory the game runs in
    settings.ini  saves/  tdata/  cache/  logs/
```

A release never writes to `disc/` or `user/`. The launcher only adds `settings.ini` keys that are missing; it
never rewrites keys the player has set.

## First-time setup

The launcher's first screen when `launcher.json` has no game data:

1. **Explain.** One paragraph: you need your own copy of *007: Nightfire* for the original Xbox; nothing is
   downloaded. A link to the README's "What you need".
2. **Choose the source.** One of:
   - **A disc image** (`.iso`): the launcher reads the Xbox file system (XDVDFS) itself. It accepts both a full
     "redump" image, whose game partition starts at offset 0x18300000, and a trimmed `xiso`. It copies out only
     `eurocom/` and `driving/`. The reader is small (a few hundred lines: the volume descriptor at sector 32, then
     a binary tree of directory entries) and needs no third-party tool.
   - **A folder** that already holds the extracted disc: the launcher checks it and copies, or offers to use it
     in place by writing its path as `DiscPath`.
3. **Check the data.**
   - The folder must contain `eurocom/filesys.d00` onwards, `eurocom/25_fps/` and `driving/`, with plausible sizes.
   - The launcher also hashes a few key files and compares them with a short table of known releases, then says
     "recognised: PAL", "recognised: NTSC (US)" or "not a version we have tested". An unknown version gets a
     warning, not a refusal: the others may well work.
4. **Copy** the data into `disc/` with a progress bar, check the copies (the same hashes), and write
   `user/settings.ini` with the absolute `DiscPath` and the defaults.
5. **Offer the basic settings**: resolution, widescreen, language, region where it matters (`Region=PAL`). Then
   **Play**.

Later, a setting can change the data folder, and "Check game data" re-runs step 3. The PS2 data (docs/ps2-data.md)
would add a second source, under its own folder, once that build is ready.

## The launcher

A small separate program, kept out of the game's code (its own target and `src/launcher/`). One window:

- **The installed version** and, when there is one, **the newer version**, with its release notes and an
  **Update** button. The channel is **Stable** (tagged releases) or **Development** (pre-releases built from `dev`).
- **Play**: starts `app/<version>/action_standalone` with the working directory `user/`. Optional "start in level"
  and "driving mission" boxes pass `-level` and `-mission` for players who want them. The window stays up and
  shows "running"; the game's output goes to `user/logs/`.
- **Settings**: the handful of `settings.ini` keys a player cares about, and "Open the settings file" for the rest.
- **Open the saves folder**, **Open the logs**, and **Report a problem**. Report a problem opens a new issue on
  GitHub with the version, the platform and the end of the last log filled in, for the player to read before
  sending.
- **Undo the last update**: switches back to the kept previous version.

It is built in the same CMake as the game, in C++. That way there is one toolchain, no runtime to install, and
the same platforms as the game. It uses:

- **SDL3** (already a dependency) for the window, with Dear ImGui for the controls;
- the system's HTTPS for downloads (WinHTTP on Windows, NSURLSession on macOS, libcurl on Linux);
- miniz for zip files;
- Monocypher for the signature check (single files).

We rejected the alternatives:

- Python with PyInstaller: large, and often flagged by antivirus programs.
- Electron or Tauri: heavy for one window.
- .NET: Windows-first.

## Releases and updates

**Building a release.** A tag `vX.Y.Z` on `main` runs a release job in CI (beside the existing CI workflow,
`.github/workflows/ci.yml`). The job:

1. builds each platform's standalone executables and the launcher: Windows x86 first (the 32-bit x87 build, the
   one exact against the original); then Windows x64, Linux x86-64 and macOS arm64, as each is ready to give
   players;
2. zips each platform's files as `nightfire-<platform>-<version>.zip`;
3. writes `manifest.json`: the version, the release date, the oldest launcher that can install this release
   (`minLauncher`), the repository (`NightfireResearch/<repo>`), and for each platform the zip's name, size and
   SHA-256;
4. signs `manifest.json` with an Ed25519 key kept as a CI secret, as `manifest.json.sig`. The public key is built
   into the launcher;
5. publishes all of it as a GitHub Release, the tag's notes as the release notes. Pre-releases from `dev`
   (nightly, or on demand) go out the same way, marked "pre-release".

The executables carry their version (from `git describe`, at build time). They print it at start and write it into
crash logs, so a report says what it came from.

**Checking for an update.** At start, and from a button, the launcher asks
`https://api.github.com/repos/NightfireResearch/<repo>/releases/latest` (Stable), or the releases list
(Development). If the newest is newer than the installed one:

1. it downloads `manifest.json` and its signature, and checks the signature;
2. it downloads its platform's zip and checks its size and SHA-256 against the manifest;
3. it unpacks the zip into `app/<new version>/`, switches `launcher.json` to it, and deletes all but the previous
   version.

Any failed check stops the update and leaves the installed version as it was. Nothing runs from a download
before its checks pass. With no network, it says so and plays the installed version.

**Updating the launcher itself.** The release carries the launcher too. A running program cannot overwrite its
own file on Windows, but it can rename it. So the launcher:

1. writes the new one beside itself as `launcher.new`;
2. renames itself to `launcher.old`, and renames `launcher.new` to `launcher`;
3. restarts, and deletes `launcher.old` on the next start.

macOS replaces the app bundle the same way, and Linux replaces the file. When a release's `minLauncher` is newer
than the running launcher, the launcher updates itself first and then installs the release: after the checks it
takes only the launcher out of the zip, puts it in place, and restarts it with `--finish-update`, and the new
launcher checks for and installs the release itself. That way an old launcher is never asked to lay out a release
it doesn't know. A restarted launcher that is still older than `minLauncher` refuses the release rather than loop,
and so does any release whose `minLauncher` is above its own version. The manifest format itself only ever gains
fields.

**Renaming the repository.** GitHub redirects a renamed repository's web, git and API addresses, so launchers in
the field keep working after a rename (the launcher follows redirects). On top of that, every manifest carries
its repository's name, and the launcher saves it, so after a rename one release moves every launcher to the new
name without relying on the redirect. If the repository does get a new name, it is one constant and one
manifest field.

## The release key

The launcher installs nothing whose `manifest.json` is not signed by the project's Ed25519 key. Its public half is
built into the launcher (`src/launcher/ReleaseKey.h`, `kReleasePublicKey`); its private half is a repository secret
that only the release job reads. Until the key exists `kReleasePublicKey` is empty: such a launcher says that
updates cannot be checked, and a release build (`NIGHTFIRE_RELEASE=ON`, which the release job sets) refuses to
configure. To make it, once:

1. `python tools/release/sign_manifest.py keygen --private-out release_key.txt` (plain Python, nothing to install)
   prints the public key and writes the private key, 64 hex digits, to `release_key.txt`.
2. On GitHub, in the repository's Settings, "Secrets and variables", Actions: a new repository secret named
   `NIGHTFIRE_RELEASE_KEY`, its value the private key. Keep a copy offline (a password manager), then delete
   `release_key.txt`. Never commit it.
3. Put the public key in `src/launcher/ReleaseKey.h` as `kReleasePublicKey`, and commit that.

The release job signs with the secret, refuses the test key (`sign --release`), and checks the signature against
`ReleaseKey.h` before it publishes anything, so a secret that does not match the launchers' key stops the release.
A lost private key means a new key and a launcher that every player installs by hand; a leaked one means the same,
at once.

`tools/release/test_key.txt` is a **test key**, whose private half is in the repository. A launcher trusts it only
when built with `NIGHTFIRE_LAUNCHER_TEST_KEY=ON`: CMake warns loudly, the window and `--version` say so, and
`NIGHTFIRE_RELEASE=ON` refuses the combination, as it refuses a `kReleasePublicKey` equal to the test key.

**Making a release.** An annotated tag `vX.Y.Z` pushed to GitHub runs `.github/workflows/release.yml`. The tag's
message becomes the release notes, and a version with a pre-release part (`v0.5.0-rc.1`) is marked pre-release.
"Run workflow" on the Actions page makes a pre-release from a branch head (`dev`) with the version typed in, tagged
`v<version>` at that commit, with GitHub's generated notes; only the Development channel offers pre-releases.
Raise `MIN_LAUNCHER` in `tools/release/package.py` when a release needs a newer launcher than the last one to lay
it out.

## Before players can be pointed at it

- **Signed programs.** Unsigned programs bring up warnings:
  - Windows SmartScreen says "Windows protected your PC" for a new unsigned program, and the player has to click
    through. A code-signing certificate removes it, or the warning fades as the program builds a reputation.
  - macOS Gatekeeper refuses an un-notarised app unless the player right-clicks Open. Notarising needs an Apple
    Developer account.
  - Linux has no such step: ship a zip, an AppImage later if wanted.

  The README should explain these warnings until the programs are signed.
- **What we test.** The PAL disc is known to work. The data check (above) also recognises the US (NTSC) disc, and
  tells players with other versions that they are trying something untested, and asks them to report how it goes.

## Order of work

| # | Step | Size |
|---|---|---|
| 1 | The executables carry their version; a release job builds, zips, writes and signs the manifest, and publishes a GitHub Release for Windows x86 | small |
| 2 | The launcher's window: version, Play (working directory `user/`, absolute `DiscPath`), Settings, the folder buttons | medium |
| 3 | First-time setup from an extracted folder: checks, copy, `settings.ini` | small |
| 4 | Updates: the check, the download, the hash and signature checks, the switch, undo | medium |
| 5 | The launcher updating itself; `minLauncher` | small |
| 6 | First-time setup from a disc image (the XDVDFS reader) | medium |
| 7 | More platforms in the release (Windows x64, Linux x86-64, macOS arm64), each once it is ready for players | small each |
| 8 | Signing and notarisation | needs the accounts |

Steps 1-4 give a first usable version: download one zip, run the launcher, point it at an extracted disc, play,
and receive updates.

## Where it stands (25 October 2026)

Built for Windows x86, steps 1-5:

- **Versions.** `cmake/version.cmake` writes `NightfireVersion` (`src/common/version.h`) from `git describe`, or
  from `NIGHTFIRE_VERSION`, which the release job sets from the tag. Both standalone executables print
  `[standalone] Nightfire <version>` at start and put it in each crash record.
- **The release job**, `.github/workflows/release.yml`: the version from the tag or the input; the windows-x86 job
  builds `action_standalone`, `driving_standalone` and the launcher with `NIGHTFIRE_RELEASE=ON`, checks that the
  executables carry the version, and zips them (`tools/release/package.py zip`); the publish job writes
  `manifest.json`, signs it (`tools/release/sign_manifest.py`, with the plain-Python Ed25519 of
  `tools/release/ed25519.py`), checks the signature against `ReleaseKey.h`, and creates the GitHub Release.
  Windows x64, Linux x86-64 and macOS arm64 are marked TODO in both files. CI's Windows job builds the launcher and
  runs the same tools on its build with the test key, as a dry run.
- **The zip** holds the executables, the launcher, FFmpeg's DLLs, `licences/` (Dear ImGui, miniz, Monocypher,
  SDL3, and FFmpeg's notes beside the DLLs) and `nightfire-release.json` (version, platform, files). The launcher
  run from the extracted zip sees that file, installs the release into the per-user folder, copies itself to the
  root and says to start it from there.
- **The launcher**, `src/launcher/`, target `nightfire_launcher` (`launcher.exe`), built with
  `-DNIGHTFIRE_LAUNCHER=ON`; Windows only so far (`Platform.h` and `Http.h` name the files a macOS or Linux port
  adds). SDL3, linked statically so that it is one file that can replace itself, with Dear ImGui on SDL's
  renderer; WinHTTP; miniz; Monocypher. First-time setup, then four tabs: **Play** (Play, an optional `-level`,
  Stop, which closes the game's window before it ends the process so that the game writes out its log, the saves, logs and game folders), **Updates** (Stable or Development, the check, the release notes,
  Update, Undo the last update), **Settings** (resolution, widescreen, language, region, open the file; the game
  data's check and change), **Help** (Report a problem, shown in full and editable before it opens a GitHub
  new-issue page; about). The game runs in a job object, with the working folder `user/` and its output in
  `user/logs/game-<time>.log`. settings.ini gets the absolute `DiscPath`, in its 8.3 form if the path is outside
  the ANSI code page, since the game opens files with narrow calls.
- **Disc images** (`src/launcher/Xdvdfs.cpp`): setup takes an `.iso` as well as a folder. The reader looks for
  the volume descriptor at sector 32 of a game partition at 0 (a trimmed xiso, as extract-xiso and similar tools
  write) or at 0x18300000, 0xFD90000 or 0x2080000 (full images of the original Xbox's discs and of the Xbox 360's
  two later disc formats, as redump-style dumps hold them), walks the directory trees with every offset and size
  checked against the table and the file, and copies out `eurocom/` and `driving/` only. The release is named from
  the image's file sizes at once ("recognised: PAL (by its files' sizes...)") and confirmed by the key files'
  hashes after the copy, which goes into `disc.partial/` and replaces `disc/` only once it is complete and checked.
  A copy from an image or a folder first checks the free space. `launcher --check-disc PATH` prints the check of an
  image or folder. Images with another layout (a CSO or other compressed image, a split image, a bare video
  partition) are refused as not an Xbox disc image.
- **Known releases** (`src/launcher/DiscSetup.cpp`): the PAL disc (the repository's `disc/`;
  `nightfire-re/iso_extract/xbox_eu` is the same release) and the US disc. Their `eurocom/filesys.d00`-`.d10` and
  `driving/` music, `.viv` and English speech are the same bytes. What differs:
  - the background movies: `eurocom/25_fps/` (25 files) on PAL, `eurocom/30_fps/` (22: no `073c0048`-`073e0048`)
    on US. The game opens `30_fps` when it runs at 60 Hz, which is `Region=NTSC` (`src/action/engine/Fmv.cpp`),
    so setup writes `Region=NTSC` for the US data, and the Settings tab warns when the region and the disc
    disagree;
  - `eurocom/config.txt`, US only, `AM`: American text (`src/action/engine/XboxSystem.cpp`);
  - `driving/misc.viv` (644,839,568 bytes PAL, 187,968,400 US), and the French, German and Spanish driving speech
    (`driving/mis*fr.spe`, `ge`, `sp`), PAL only;
  - `default.xbe` and `Driving.xbe`, which the standalone executables do not read and setup does not copy.
- **Tests.** `launcher --selftest` (a test-key build) runs the window's own code without the window: setup into a
  fresh folder, the first install from an extracted release, no network, a bad signature, a bad hash, a good update
  (the launcher replaced by renaming), the minLauncher path (the launcher first, a still-old launcher's refusal,
  then the install), undo, and the game started for a while. `--disc` may be a disc image; `--image ISO
  --reference FOLDER` (repeated) checks and extracts an image and compares every file with the folder it was made
  from, and `--bad-image FILE` (repeated) must be refused. `tools/release/make_test_xiso.py` builds such images
  from an extracted disc, as an xiso or with `--redump` as a full image, and small damaged ones with `--synthetic
  --damage truncate|dirloop|badoffset`; they are about the size of the disc, so make them where there is room. `tools/release/fake_releases.py` makes its fake
  releases API, signed with the test key, as files (`file://`) or served on 127.0.0.1 (WinHTTP, redirects
  included):

  ```
  cmake --preset windows -DNIGHTFIRE_LAUNCHER=ON -DNIGHTFIRE_LAUNCHER_TEST_KEY=ON
  cmake --build --preset windows --target action_standalone driving_standalone nightfire_launcher
  python tools/release/fake_releases.py --build build/windows/Release --out fake --serve 8765
  build/windows/Release/launcher.exe --selftest --root selftest --disc disc --api-base http://127.0.0.1:8765 --bundle fake/bundle
  ```

  `--in-place` uses the disc where it is instead of copying 3 GB, and `--play-seconds 0` skips the game. The
  window takes `--root`, `--api-base`, `--repository` and `--no-update-check` too, and
  `--screenshot FILE.bmp [--tab NAME]` draws it once and exits.

Left for later:

- more platforms (step 7: a `PlatformPosix.cpp`, `Http` files for NSURLSession and
  libcurl, their jobs in `release.yml`), and signing and notarisation (step 8);
- **a choice of game data**: the PS2 and GameCube data builds, once they are ready, with the differences explained
  and players steered to the better assets (the GameCube audio is poor). It belongs in setup, as a source, and in
  the Settings tab's Game data section; `launcher.json` would hold one entry per data set beside `discPath`, each
  in its own folder under `disc/`;
- **mods** (docs/modding.md, on the modding-design branch): a Mods tab for the built-in modules, and a profile
  choice beside Play (Default: PC controls and bug fixes; Speedrun: bug fixes, time-demo recording and autosplit
  only) passed to the one executable. Nothing in the launcher's layout or `launcher.json` stands in the way of
  either.
