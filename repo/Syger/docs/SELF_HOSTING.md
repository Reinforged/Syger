# Syger self-hosting compiler

Syger now has a first self-hosted compiler pipeline. `selfhost/compiler.sg` is
written in Syger. It tokenizes and parses a `.sg` program and emits C that
implements the program directly using `selfhost/native_runtime.c`.

The existing C executable remains the **stage0 bootstrap interpreter**. It is
needed to execute the Syger-written compiler for the first time. The resulting
native compiler is stage1; it compiles `selfhost/compiler.sg` again to produce
stage2. Stage2 compiles the same compiler source to stage3, and the generated
stage2 and stage3 C files must be byte-for-byte identical.

This is a C-backend compiler: generated programs are compiled by Clang and link
the Syger native runtime. The compiler frontend and code generator are Syger
source; the runtime remains C. More advanced source-mapped diagnostics and
additional backends are separate future work.

## Build and bootstrap test

From the Syger repository root, run:

```bash
./scripts/bootstrap-selfhost.sh
```

The script builds stage0, compiles the Syger-written compiler into stage1,
then performs stage1 → stage2 → stage3 bootstrap verification. It also compiles
`examples/hello.sg` and a compiler smoke test to native executables and compares
their output against the stage0 interpreter, tests the host primitives, checks
that invalid syntax is rejected, and tests the `sygerc` wrapper.

## Manual commands

Build the bootstrap interpreter:

```bash
clang -Wall -Wextra -std=c17 compiler/*.c -o Syger
```

Run the complete language regression suite:

```bash
./Syger examples/hello.sg
```

Compile a Syger file into a native executable:

```bash
./scripts/sygerc examples/hello.sg -o build/hello
```

Run it:

```bash
./build/hello
```

To inspect the generated C as well, set `SYGER_KEEP_C=1` when invoking
`scripts/sygerc`; it prints the temporary directory containing `program.c`.

The CLI accepts arguments after the `.sg` script path; `args()` returns those
arguments. The compiler uses `read_file`, `write_file`, `char_at`, `substring`,
`token_at`, `type_of`, `to_string`, `chr`, and `fail` as bootstrap/runtime
primitives.
