#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/selfhost"
mkdir -p "$BUILD"
cd "$ROOT"

CC="${CC:-clang}"
CFLAGS=(-Wall -Wextra -std=c17)

printf '[1/9] Building stage0 Syger interpreter...\n'
"$CC" "${CFLAGS[@]}" compiler/*.c -o "$BUILD/Syger-stage0"

printf '[2/9] Running stage0 regression suite...\n'
"$BUILD/Syger-stage0" examples/hello.sg > "$BUILD/hello.stage0.out"
grep -q '=== ALL TESTS COMPLETE ===' "$BUILD/hello.stage0.out"

printf '[3/9] Testing self-hosting primitives...\n'
"$BUILD/Syger-stage0" tests/selfhost/primitives.sg "$BUILD/primitives.stage0.txt" > "$BUILD/primitives.stage0.out"
cmp tests/selfhost/primitives.expected.txt "$BUILD/primitives.stage0.out"
if "$BUILD/Syger-stage0" selfhost/compiler.sg tests/selfhost/invalid.sg "$BUILD/invalid.c" > /dev/null 2>&1; then
    echo "Self-hosted compiler accepted an invalid program unexpectedly." >&2
    exit 1
fi

printf '[4/9] Stage0 compiling the Syger-written compiler...\n'
"$BUILD/Syger-stage0" selfhost/compiler.sg "$ROOT/selfhost/compiler.sg" "$BUILD/SygerCompiler-stage1.c" > /dev/null
"$CC" "${CFLAGS[@]}" -I selfhost "$BUILD/SygerCompiler-stage1.c" selfhost/native_runtime.c -o "$BUILD/SygerCompiler-stage1"

printf '[5/9] Compiling and testing source programs with stage1...\n'
"$BUILD/SygerCompiler-stage1" examples/hello.sg "$BUILD/hello.stage1.c" > /dev/null
"$CC" "${CFLAGS[@]}" -I selfhost "$BUILD/hello.stage1.c" selfhost/native_runtime.c -o "$BUILD/hello.stage1"
"$BUILD/SygerCompiler-stage1" tests/selfhost/compiler_smoke.sg "$BUILD/smoke.stage1.c" > /dev/null
"$CC" "${CFLAGS[@]}" -I selfhost "$BUILD/smoke.stage1.c" selfhost/native_runtime.c -o "$BUILD/smoke.stage1"
"$BUILD/SygerCompiler-stage1" tests/selfhost/primitives.sg "$BUILD/primitives.stage1.c" > /dev/null
"$CC" "${CFLAGS[@]}" -I selfhost "$BUILD/primitives.stage1.c" selfhost/native_runtime.c -o "$BUILD/primitives.stage1"
"$BUILD/Syger-stage0" examples/hello.sg > "$BUILD/hello.interpreter.out"
"$BUILD/hello.stage1" > "$BUILD/hello.native.out"
cmp "$BUILD/hello.interpreter.out" "$BUILD/hello.native.out"
"$BUILD/Syger-stage0" tests/selfhost/compiler_smoke.sg > "$BUILD/smoke.interpreter.out"
"$BUILD/smoke.stage1" > "$BUILD/smoke.native.out"
cmp "$BUILD/smoke.interpreter.out" "$BUILD/smoke.native.out"
"$BUILD/primitives.stage1" "$BUILD/primitives.stage1.txt" > "$BUILD/primitives.native.out"
cmp tests/selfhost/primitives.expected.txt "$BUILD/primitives.native.out"
SYGER_BOOTSTRAP="$BUILD/Syger-stage0" "$ROOT/scripts/sygerc" tests/selfhost/compiler_smoke.sg -o "$BUILD/smoke.wrapper"
"$BUILD/smoke.wrapper" > "$BUILD/smoke.wrapper.out"
cmp "$BUILD/smoke.interpreter.out" "$BUILD/smoke.wrapper.out"

printf '[6/9] Stage1 compiling itself to stage2...\n'
"$BUILD/SygerCompiler-stage1" selfhost/compiler.sg "$BUILD/SygerCompiler-stage2.c" > /dev/null
"$CC" "${CFLAGS[@]}" -I selfhost "$BUILD/SygerCompiler-stage2.c" selfhost/native_runtime.c -o "$BUILD/SygerCompiler-stage2"

printf '[7/9] Stage2 compiling itself to stage3...\n'
"$BUILD/SygerCompiler-stage2" selfhost/compiler.sg "$BUILD/SygerCompiler-stage3.c" > /dev/null
cmp "$BUILD/SygerCompiler-stage2.c" "$BUILD/SygerCompiler-stage3.c"
"$CC" "${CFLAGS[@]}" -I selfhost "$BUILD/SygerCompiler-stage3.c" selfhost/native_runtime.c -o "$BUILD/SygerCompiler-stage3"

printf '[8/9] Checking stage1/stage2 output stability...\n'
"$BUILD/SygerCompiler-stage2" examples/hello.sg "$BUILD/hello.stage2.c" > /dev/null
cmp "$BUILD/hello.stage1.c" "$BUILD/hello.stage2.c"
"$BUILD/SygerCompiler-stage2" examples/hello.sg "$BUILD/hello.stage3.c" > /dev/null
cmp "$BUILD/hello.stage2.c" "$BUILD/hello.stage3.c"

printf '[9/9] Wrapper and native-output verification passed.\n'
printf '\nSelf-host bootstrap passed. Stage2 and stage3 compiler C output is identical.\n'
printf 'Build artifacts: %s\n' "$BUILD"
