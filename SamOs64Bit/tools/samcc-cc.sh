#!/bin/bash
# A gcc stand-in that compiles C with SamCompiler.
#
#   tools/samcc-cc.sh <gcc> <arch> <gcc arguments...>
#
# Every .c argument is compiled by SamCompiler
# (-target=samos-<arch>, the -I directories of the command line) to an
# object; everything else, .S files and the link included, goes to
# <gcc> unchanged with those objects in place of the .c files. So
# `make USERCC=samcc` swaps the C compiler of the user programs and
# keeps the assembler, crt0 sources and linker of the gcc build.
# The compiler is found as the samcc tests find it (tests64/samcc.sh:
# $SAMCC when set, else SamCompiler/main next to this repository);
# without one the build stops.
set -euo pipefail
gcc=$1 arch=$2
shift 2
. "$(dirname "$0")/../tests64/samcc.sh"
if ! samcc_find "$arch"; then
    echo "samcc-cc: $SAMCC_SKIP" >&2
    exit 1
fi
samcc=$SAMCC_BIN

incs=() srcs=() out="" compile_only=0
args=("$@")
for ((i = 0; i < ${#args[@]}; i++)); do
    a=${args[i]}
    case "$a" in
    -I) incs+=("-I${args[i + 1]}") ;;
    -I*) incs+=("$a") ;;
    -o) out=${args[i + 1]} ;;
    -c) compile_only=1 ;;
    *.c) srcs+=("$a") ;;
    esac
done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
# samcc <src> <object>: SamCompiler writes <asm> and <asm>.o.
samcc_one() {
    local asm="$tmp/$(basename "$1").s"
    if ! "$samcc" -target=samos-$arch -sysroot="$tmp" "${incs[@]}" "$1" "$asm" object > "$asm.log" 2>&1 \
        || [ ! -f "$asm.o" ]; then
        echo "samcc-cc: SamCompiler failed on $1:" >&2
        grep -v '^\(everything compiled fine\|nasm\|.*-as \)' "$asm.log" | tail -n 5 >&2
        exit 1
    fi
    mv "$asm.o" "$2"
}

if [ $compile_only -eq 1 ] && [ ${#srcs[@]} -eq 1 ]; then
    samcc_one "${srcs[0]}" "${out:-$(basename "${srcs[0]}" .c).o}"
    exit 0
fi
cmd=()
for a in "$@"; do
    case "$a" in
    *.c)
        obj="$tmp/$(basename "$a" .c).$RANDOM.o"
        samcc_one "$a" "$obj"
        cmd+=("$obj")
        ;;
    *) cmd+=("$a") ;;
    esac
done
"$gcc" "${cmd[@]}"
