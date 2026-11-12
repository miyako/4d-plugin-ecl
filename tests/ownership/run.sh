#!/bin/bash
# Builds the synthetic-boundary ownership harness against a source tree with
# ASan/UBSan and runs every test in its own process.
#
#   tests/ownership/run.sh <source-root> <fixed|baseline>
#
# <source-root> provides the plugin sources, the bundled 4D Plugin API and the
# bundled ECL headers. The harness and the ECL stub always come from this
# script's directory, so the SAME harness runs against an immutable baseline
# (git archive of the base SHA) and against the fixed tree. libecl is never
# linked and no Lisp is evaluated.
#
# In "fixed" mode every test must pass. In "baseline" mode the tests named in
# EXPECTED_BASELINE_FAILURES must fail and every other test (the controls)
# must pass; anything else is a harness or model error.
set -euo pipefail

SRC=$(cd "$1" && pwd)
MODE=$2
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

EXPECTED_BASELINE_FAILURES="
defect_cmd_lisp_eval_failure
defect_cmd_lisp_missing_parameter
defect_cmd_lisp_success
defect_ob_get_a_result_not_cleared
defect_ob_get_b_wrong_kind_not_cleared
defect_ob_get_c_reference_not_released
defect_ob_get_d_wrong_kind_not_cleared
defect_ob_get_n_wrong_kind_not_cleared
defect_ob_get_o_reference_not_released
defect_ob_get_s_result_not_cleared
defect_ob_set_a_u16_empty_owner
defect_ob_set_a_wchar_empty_owner
defect_ob_set_p_empty_owner
defect_ob_set_s_ckey_empty_owner
defect_ob_set_s_wkey_empty_owner
guard_get_error_result_untouched
guard_ob_stringify_error_result_untouched
"

SAN="-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined"
INC=(-I "$SRC/4D Plugin API" -I "$SRC/support" -I "$SRC/include" -I "$SRC")
WARN="-w"

objs=()
cc() { # <lang> <file> [extra flags...]
    local lang=$1 file=$2; shift 2
    local o="$OUT/$(basename "$file").o"
    xcrun clang $SAN $WARN -x "$lang" "${INC[@]}" "$@" -c "$file" -o "$o"
    objs+=("$o")
}

# The bundled ECL headers do not compile as C++17 ('register'), and the
# project builds with C++11 (c++0x).
cc c "$SRC/4D Plugin API/4DPluginAPI.c" -Wno-multichar -Wno-error=int-conversion
cc objective-c++ "$SRC/support/4DPlugin-JSON.cpp" -std=c++11
cc objective-c++ "$SRC/4DPlugin-ECL.cpp" -std=c++11
cc c++ "$HERE/ecl_stub.cpp" -std=c++11
cc c++ "$HERE/engine_harness.cpp" -std=c++17

xcrun clang++ $SAN "${objs[@]}" -framework Foundation -framework CoreFoundation -framework CoreGraphics -o "$OUT/harness"

export ASAN_OPTIONS=abort_on_error=0:halt_on_error=1
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1

bad=0
for t in $("$OUT/harness" --list); do
    if "$OUT/harness" "$t" >"$OUT/$t.log" 2>&1; then got=pass; else got=fail; fi
    expect=pass
    if [ "$MODE" = baseline ] && grep -qx "$t" <<<"$EXPECTED_BASELINE_FAILURES"; then expect=fail; fi
    if [ "$got" = "$expect" ]; then status=OK; else status=UNEXPECTED; bad=1; fi
    printf '%-12s %-5s (expected %-4s) %s\n' "$status" "$got" "$expect" "$t"
    if [ "$got" = fail ]; then sed 's/^/    | /' "$OUT/$t.log"; fi
    # A sanitizer report is never an expected outcome in either mode.
    if grep -qE 'ERROR: AddressSanitizer|runtime error:|ENGINE VIOLATION' "$OUT/$t.log"; then
        echo "UNEXPECTED   sanitizer/engine violation in $t"; bad=1
    fi
done

if [ $bad -ne 0 ]; then echo "RESULT ($MODE): FAILED"; exit 1; fi
echo "RESULT ($MODE): all outcomes as expected"
