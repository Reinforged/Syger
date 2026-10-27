
## Native compilation and self-hosting

Syger includes a compiler written in Syger that translates `.sg` programs to C,
then uses Clang to produce a native executable. The C interpreter remains the
stage0 bootstrap. Run the repeatable stage0 → stage1 → stage2 → stage3 check
with:

```bash
clang -Wall -Wextra -std=c17 compiler/*.c -o Syger
./scripts/bootstrap-selfhost.sh
```

Compile an individual Syger program to a native executable with:

```bash
./scripts/sygerc examples/hello.sg -o build/hello
./build/hello
```

Details and the bootstrap architecture are documented in
[`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md).
