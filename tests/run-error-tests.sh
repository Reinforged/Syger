#!/bin/bash
# Checks error messages. For each tests/errors/NAME.sg:
#  - the interpreter's stderr must equal tests/errors/NAME.expected exactly;
#  - the native compiler (at compile time or when the program runs) must
#    report the same error code, message and file:line.
# Usage: tests/run-error-tests.sh <interpreter> <native-compiler>   (from the repo root)
# Set UPDATE=1 to rewrite the .expected files from the interpreter.
set -uo pipefail

INTERPRETER="$1"
COMPILER="$2"
CC="${CC:-clang}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/syger-errors.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
export NO_COLOR=1
failures=0

for source in tests/errors/*.sg; do
    name="$(basename "$source" .sg)"
    expected="tests/errors/$name.expected"

    "$INTERPRETER" "$source" > /dev/null 2> "$WORK/interp.err"
    if [[ "${UPDATE:-0}" == "1" ]]; then
        cp "$WORK/interp.err" "$expected"
    fi
    if ! cmp -s "$expected" "$WORK/interp.err"; then
        echo "FAIL (interpreter) $source"
        diff "$expected" "$WORK/interp.err" | head -20
        failures=$((failures + 1))
        continue
    fi

    if "$COMPILER" "$source" "$WORK/program.c" > /dev/null 2> "$WORK/native.err"; then
        "$CC" -std=gnu17 -O2 -w -I selfhost "$WORK/program.c" selfhost/native_runtime.c -o "$WORK/program"
        "$WORK/program" > /dev/null 2> "$WORK/native.err"
    fi

    # Header and location must agree; the interpreter may add a column.
    want="$(head -2 "$expected" | sed -E 's/(:[0-9]+):[0-9]+$/\1/')"
    got="$(head -2 "$WORK/native.err")"
    if [[ "$want" != "$got" ]]; then
        echo "FAIL (native) $source"
        echo "  expected: $want"
        echo "  got:      $got"
        failures=$((failures + 1))
    fi
done

if [[ $failures -gt 0 ]]; then
    echo "$failures error test(s) failed"
    exit 1
fi
echo "All error tests passed."
