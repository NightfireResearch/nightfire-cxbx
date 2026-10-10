#!/bin/sh
# Runs the action engine from a fresh boot (no psiLaunch.bin: language, intro, start page) with a MenuProbe
# replay script, in its own folder build/menurun/<name>/ with a copy of Release/saves, then converts the screenshots to PNG and writes the
# named message log. The first run creates the build/menurun/disc junction the loader needs (it looks for
# ../disc/default.xbe).
#
#   tools/ui/run_menu.sh tools/ui/scripts/front_end.txt [name] [timeout seconds]
#   ORIGINAL=0x40000030:0x8ded0,0x76470 tools/ui/run_menu.sh ...   # run those as the original code (MenuOriginal)
#   SETTINGS='MPMatchShadow=on;BotCoreShadow=on' tools/ui/run_menu.sh ...   # extra settings.ini lines
#
# Output: build/menurun/<name>/run.log (everything), menu.log (named messages), menu_shots/*.png.
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SCRIPT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
NAME=${2:-$(basename "$1" .txt)}
TIMEOUT=${3:-300}
RUN="$REPO/build/menurun/$NAME"
mkdir -p "$RUN"
if [ ! -e "$REPO/build/menurun/disc/default.xbe" ]; then
    powershell -NoProfile -Command "New-Item -ItemType Junction -Path '$(cygpath -w "$REPO/build/menurun/disc")' -Target '$(cygpath -w "$REPO/disc")' | Out-Null"
fi
# The game's own settings, without the disc path (the junction stands in for it), plus the probes.
grep -v -i -E '^(DiscPath|MenuLog|MenuLogSkip|MenuScript|MenuOriginal|MenuCheckLists)=' "$REPO/Release/settings.ini" > "$RUN/settings.ini"
printf '\nMenuCheckLists=on\nMenuLog=on\nMenuLogSkip=0x50,0x51\nMenuScript=%s\nMenuOriginal=%s\n' "$(cygpath -m "$SCRIPT")" "${ORIGINAL:-}" >> "$RUN/settings.ini"
[ -n "${SETTINGS:-}" ] && printf '%s\n' "$SETTINGS" | tr ';' '\n' >> "$RUN/settings.ini"
rm -f "$RUN/psiLaunch.bin" "$RUN"/menu_shots/*.png
# The language marker a previous run left on t:\ (tdata/lang*.dat) skips the language page and shifts every
# frame after it, so each run starts without one.
rm -rf "$RUN/tdata"
# A copy of the codename saves (Release/saves), so the start page leads to codename select and the main menu
# rather than straight into the first mission; the run changes the copy, never the originals.
rm -rf "$RUN/saves"
[ -d "$REPO/Release/saves" ] && cp -r "$REPO/Release/saves" "$RUN/saves"
cd "$RUN"
timeout "$TIMEOUT" "$REPO/Release/action.exe" > run.log 2>&1 || true
python "$REPO/tools/ui/menu_log.py" run.log > menu.log
for f in menu_shots/*.bmp; do
    [ -e "$f" ] && python "$REPO/tools/bmp2png.py" "$f" > /dev/null && rm -f "$f"
done
echo "$RUN: $(grep -c '^f=' menu.log) messages, $(ls menu_shots/*.png 2>/dev/null | wc -l) screenshots"
