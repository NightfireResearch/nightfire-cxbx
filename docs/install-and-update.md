# Installing and updating: a proposal

How someone who is not a developer could get this project onto their computer, point it at their own copy of
the game, and keep it up to date. Nothing here is built yet; this is the design and its order of work
(24 October 2026).

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
     "recognised: PAL" or "not a version we have tested". An unknown version gets a warning, not a refusal: we
     have tested one release, and the others may well work.
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
than the running launcher, the launcher updates itself first and then installs the release. That way an old
launcher is never asked to read a format it doesn't know. The manifest format itself only ever gains fields.

**Renaming the repository.** GitHub redirects a renamed repository's web, git and API addresses, so launchers in
the field keep working after a rename (the launcher follows redirects). On top of that, every manifest carries
its repository's name, and the launcher saves it, so after a rename one release moves every launcher to the new
name without relying on the redirect. If the repository does get a new name, it is one constant and one
manifest field.

## Before players can be pointed at it

- **A licence.** The repository has no LICENSE file. Without one, nobody else may legally redistribute it, so the
  project should choose a licence before publishing binaries. The README's "What you need" already says the game
  data is never included.
- **Signed programs.** Unsigned programs bring up warnings:
  - Windows SmartScreen says "Windows protected your PC" for a new unsigned program, and the player has to click
    through. A code-signing certificate removes it, or the warning fades as the program builds a reputation.
  - macOS Gatekeeper refuses an un-notarised app unless the player right-clicks Open. Notarising needs an Apple
    Developer account.
  - Linux has no such step: ship a zip, an AppImage later if wanted.

  The README should explain these warnings until the programs are signed.
- **What we test.** One disc release (PAL) is known to work. The data check (above) tells players with other
  versions that they are trying something untested, and asks them to report how it goes.

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
