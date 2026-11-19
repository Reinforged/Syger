# Syger

Syger is a small language with Python-style indentation. You can run a program with the interpreter, or compile it to a native executable with a compiler that is itself written in Syger.

```syger
fun count_words(text)
    count = 0
    in_word = false
    i = 0
    while i < length(text)
        if char_at(text, i) == " "
            in_word = false
        elif not in_word
            in_word = true
            count = count + 1
        i = i + 1
    return count

print(count_words("the quick brown fox"))   # 4
```

## Build and run

You need Clang.

```bash
clang -Wall -Wextra -std=c17 -O2 compiler/*.c -o Syger
./Syger examples/hello.sg
```

Arguments after the file name are available to the program through `args()`.

## Compile to a native executable

```bash
./scripts/sygerc examples/hello.sg -o build/hello
./build/hello
```

`sygerc` runs `selfhost/compiler.sg` to turn the program into C, then compiles that with Clang. Set `SYGER_KEEP_C=1` to keep the generated C. Compiled programs are faster than the interpreter, most noticeably in code that calls a lot of functions.

## Testing

Run everything:

```bash
./scripts/bootstrap-selfhost.sh
```

This builds the interpreter, uses it to compile the Syger compiler, then has that compiler compile itself twice and checks the two results are identical. Along the way it runs the test programs on both the interpreter and the compiled versions and checks they print the same thing, and checks every error message in `tests/errors/`. If it ends with `Self-host bootstrap passed`, everything works.

Smaller checks while you work:

```bash
./Syger examples/hello.sg                    # language tour, ends with ALL TESTS COMPLETE
./Syger tests/selfhost/semantics.sg | diff tests/selfhost/semantics.expected.txt -
tests/run-error-tests.sh ./Syger build/selfhost/SygerCompiler-stage1
```

After changing an error message on purpose, regenerate the expected output with `UPDATE=1 tests/run-error-tests.sh ...`.

## How Syger differs from Python

- **Blocks need a header and indentation, no colon:** `if x > 3`, `while i < 10`, `fun add(a, b)`.
- **Conditions must be booleans.** `if count` is an error; write `if count > 0`.
- **Arrays are values.** `b = a` gives `b` its own copy, and a function that changes an array parameter doesn't change the caller's array. Copies are cheap: they are only made when one side actually changes.
- **Functions see their own variables and globals, nothing else.** Assigning to a name inside a function creates a local. To update shared state from a function, keep it in a global array and change an element: `counter[0] = counter[0] + 1`.
- **No string escapes yet.** Use `chr(34)` for `"` and `chr(10)` for a newline.
- Only integers for now. `/` is integer division.

## Built-in functions

| | |
| --- | --- |
| Output | `print(...)`, `eprint(...)` (to stderr) |
| Arrays | `length`, `append`, `pop`, `insert`, `remove`, `clear`, `reverse`, `sort`, `slice`, `contains`, `index_of`, `first`, `last`, `sum`, `min`, `max`, `join` |
| Strings | `length`, `char_at`, `substring`, `chr`, `to_string` |
| Files | `read_file(path)`, `write_file(path, text)` |
| Program | `args()`, `exit(code)`, `fail(message)`, `type_of(value)` |

Functions that change an array (`append`, `pop`, `insert`, `remove`, `clear`, `reverse`, `sort`) take it as a variable, e.g. `append(items, 4)`.

## Errors

Every error has a code, a location and usually a hint:

```
error[E3001]: variable 'totl' is not defined
 --> report.sg:3
  |
3 | print(totl + count)
  |
  = help: did you mean 'total'?
```

All codes are explained in [docs/ERRORS.md](docs/ERRORS.md).

## Layout

```
compiler/   interpreter in C (lexer, parser, interpreter, values, diagnostics)
selfhost/   compiler written in Syger, plus the C runtime compiled programs use
scripts/    sygerc and the bootstrap test
tests/      semantics test, error tests
docs/       error reference, self-hosting notes
examples/   hello.sg, a tour of the language
```

How the self-hosting bootstrap works is described in [docs/SELF_HOSTING.md](docs/SELF_HOSTING.md).

## License

No license yet.
