#!/bin/bash
# tools/fused_ops.sh <binary>...: the fused multiply-adds (FMADD, FMSUB, FNMADD, FNMSUB and the vector FMLA/FMLS) in
# arm64 code, by function (docs/driving/macos.md 4: the game is built with -ffp-contract=off). Exits 1 if any is in a
# function other than the two whose fused operation the source asks for and is exact either way, as
# tools/arm64_probe.sh lists them. OBJDUMP names another objdump (aarch64-linux-gnu-objdump for Linux objects).
set -u
expected='ShaderBuilder::Mad|Crt::CIpow'
fused=0
unexpected=0
for binary in "$@"; do
    while read -r n function; do
        if echo "$function" | grep -qE "^($expected)\("; then
            echo "  $n fused in $function (expected)"
        else
            echo "  $n fused in $function"
            unexpected=$((unexpected + n))
        fi
        fused=$((fused + n))
    done < <("${OBJDUMP:-objdump}" -d -C --no-show-raw-insn "$binary" |
             awk '/^[0-9a-f]+ <.*>:$/ { f = substr($0, index($0, "<") + 1); sub(/>:$/, "", f) }
                  /[ \t](fn?madd|fn?msub|fmla|fmls)[ \t]/ { n[f]++ } END { for (f in n) print n[f], f }')
done
echo "fused multiply-adds: $fused, $unexpected not expected"
[ "$unexpected" = 0 ]
