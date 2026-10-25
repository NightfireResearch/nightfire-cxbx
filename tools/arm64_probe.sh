#!/bin/bash
# tools/arm64_probe.sh [build-dir]: the arm64 compile probe (docs/driving/macos.md, package M2). Run inside the
# nf-cross-arm64 image (nf-cross plus g++-aarch64-linux-gnu, binutils-aarch64-linux-gnu and lld), from the source
# tree:
#
#   MSYS_NO_PATHCONV=1 docker run --rm -v "Q:/nightfire-cxbx:/src" -w /src nf-cross-arm64 bash tools/arm64_probe.sh
#
# Configures the standalone driving engine for aarch64-linux-gnu (cmake/toolchain-linux-arm64-probe.cmake, SDL
# headless), compiles every object of driving_standalone and driving_standalone_test, keeping on after errors, and
# counts the fused multiply-adds (FMADD, FMSUB, FNMADD, FNMSUB and the vector FMLA/FMLS) in the game's objects -
# everything but SDL's - by function. Errors and unexpected fused operations should both be 0. Nothing is linked.
set -u
B=${1:-build/linux-arm64-probe}
cmake -S . -B "$B" -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux-arm64-probe.cmake -DCMAKE_BUILD_TYPE=Release \
      -DNIGHTFIRE_SDL=ON -DSDL_UNIX_CONSOLE_BUILD=ON > "$B.configure.log" 2>&1 ||
    { echo "configure failed: the end of $B.configure.log:"; tail -30 "$B.configure.log"; exit 1; }
objects=$(ninja -C "$B" -t targets all | grep -oE '^CMakeFiles/driving_standalone(_test)?\.dir/[^:]+\.o' | sort -u)
ninja -C "$B" -k 0 $objects > "$B.build.log" 2>&1
errors=$(grep -c ' error: ' "$B.build.log")
failed=$(grep -c '^FAILED: ' "$B.build.log")
echo "compile: $(echo "$objects" | wc -w) objects, $failed failed, $errors errors (log: $B.build.log)"
grep ' error: ' "$B.build.log" | sed -E 's/^.*\/src\///' | sort | uniq -c | sort -rn | head -40
# Fused operations the source asks for, exact either way: ShaderBuilder::Mad folds a literal MAD with fmaf, and
# PowEdge's fmod(y, 2.0), inlined into Crt::CIpow, is expanded by LLVM as y - 2 * trunc(y / 2) with one FMADD
expected='ShaderBuilder::Mad|Crt::CIpow'
fused=0
unexpected=0
for o in $objects; do
    [ -f "$B/$o" ] || continue
    while read -r n function; do
        if echo "$function" | grep -qE "^($expected)\("; then
            echo "  $n fused in $function ($o, expected)"
        else
            echo "  $n fused in $function ($o)"
            unexpected=$((unexpected + n))
        fi
        fused=$((fused + n))
    done < <(aarch64-linux-gnu-objdump -d -C --no-show-raw-insn "$B/$o" |
             awk '/^[0-9a-f]+ <.*>:$/ { f = substr($0, index($0, "<") + 1); sub(/>:$/, "", f) }
                  /[ \t](fn?madd|fn?msub|fmla|fmls)[ \t]/ { n[f]++ } END { for (f in n) print n[f], f }')
done
echo "fused multiply-adds in the game's objects: $fused, $unexpected not expected"
