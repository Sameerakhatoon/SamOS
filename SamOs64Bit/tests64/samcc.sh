# tests64/samcc.sh - the one place that finds a SamCompiler
# with the SamOS targets, for e2e/208 (samos-x86_64) and
# arch/<arch>-samcc.sh (samos-aarch64, samos-riscv64). Sourced.
#
#   samcc_find <arch>   sets SAMCC_BIN and returns 0, or sets
#                       SAMCC_SKIP (the reason to skip) and returns 1
#
# Candidates, first that accepts -target=samos-<arch> wins: $SAMCC
# alone when it is set; else SamCompiler/main next to this repository
# (a SamCompiler clone beside the SamOS one, built with its build.sh).
# A missing
# compiler is a SKIP, never a FAIL. The probe keeps the output in a
# variable: under pipefail, `samcc ... | grep` takes samcc's exit
# status 1 and never matches (e2e/208).
samcc_accepts() {
    local out probe="${TMPDIR:-/tmp}/samcc-probe.$$"
    [ -x "$1" ] || return 1
    out=$(SAMOS_SYSROOT= "$1" -target="samos-$2" /dev/null "$probe" 2>&1)
    rm -f "$probe"
    case "$out" in *"Unknown target"*) return 1 ;; esac
    return 0
}

samcc_find() {
    local c tried=""
    SAMCC_BIN=
    SAMCC_SKIP=
    if [ -n "${SAMCC:-}" ]; then
        set -- "$1" "$SAMCC"
    else
        set -- "$1" "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/SamCompiler/main"
    fi
    for c in "${@:2}"; do
        if samcc_accepts "$c" "$1"; then
            SAMCC_BIN=$c
            return 0
        fi
        tried="$tried${tried:+, }$c"
    done
    SAMCC_SKIP="no SamCompiler with -target=samos-$1 (tried $tried; set SAMCC)"
    return 1
}
