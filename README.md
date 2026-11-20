# Syger

Syger is a small language with Python-style indentation. You can run a program with the interpreter, or compile it to a native executable with a compiler that is itself written in Syger.

Its party trick is `import`: point it at a JSON, CSV, TSV, `.env` or Python file and you get the data as ordinary Syger values.

```syger
import "data/team.json" as team
import "data/settings.py" as settings      # reads the constants, never runs the Python

for member in team["members"]
    print(member["name"] + ": " + member["role"])

print(settings["TAX_RATE"] * 100)
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

## Importing data

```syger
import "config.json" as config     # objects become maps, arrays become arrays
import "people.csv" as people      # an array of maps, one per row, keyed by the header
import "scores.tsv" as scores      # same, tab-separated
import ".env" as env               # a map of strings
import "constants.py" as py        # a map of the top-level NAME = literal constants
import "notes.txt"                 # anything else: the text; named notes from the file name
```

- Paths are relative to the `.sg` file doing the import.
- CSV fields that look exactly like numbers become numbers; `"007"` and quoted fields stay text.
- From Python files, only constants made of literals are read (numbers, strings, lists, tuples, dicts, `True`/`False`/`None`). Anything computed is skipped, and the file is never run.
- When compiling with `sygerc`, the file is read and checked at compile time and baked into the executable, so the program doesn't need the file at runtime. A broken data file is a compile error that points at the line in the data file.
- `load(path)` does the same at runtime for paths you only know while running, and `load_error(path)` returns what's wrong with a file (or `""`) without stopping the program.

See [examples/import_demo.sg](examples/import_demo.sg).

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
./Syger tests/selfhost/features.sg | diff tests/selfhost/features.expected.txt -
tests/run-error-tests.sh ./Syger build/selfhost/SygerCompiler-stage1
```

After changing an error message on purpose, regenerate the expected output with `UPDATE=1 tests/run-error-tests.sh ...`.

## How Syger differs from Python

- **Blocks need a header and indentation, no colon:** `if x > 3`, `while i < 10`, `fun add(a, b)`.
- **Conditions must be booleans.** `if count` is an error; write `if count > 0`.
- **Arrays and maps are values.** `b = a` gives `b` its own copy, and a function that changes an array parameter doesn't change the caller's array. Copies are cheap: they are only made when one side actually changes.
- **Functions see their own variables and globals, nothing else.** Assigning to a name inside a function creates a local. To update shared state from a function, keep it in a global array and change an element: `counter[0] = counter[0] + 1`.
- **`/` on two integers is integer division** (`7 / 2` is `3`); use a float to get `3.5` (`7.0 / 2`).
- **Map keys are strings or integers**, and maps keep the order keys were added. Reading a missing key is an error; use `get(map, key, default)` or `contains(map, key)`.
- `for` loops over arrays, map keys, string characters and `range(...)`. Brackets can span several lines.

## Built-in functions

| | |
| --- | --- |
| Output | `print(...)`, `eprint(...)` (to stderr) |
| Arrays | `length`, `append`, `pop`, `insert`, `remove`, `clear`, `reverse`, `sort`, `slice`, `contains`, `index_of`, `first`, `last`, `sum`, `min`, `max`, `join`, `range` |
| Maps | `length`, `keys`, `values`, `get`, `contains`, `remove` |
| Strings | `length`, `split`, `trim`, `upper`, `lower`, `replace`, `starts_with`, `ends_with`, `contains`, `index_of`, `char_at`, `substring`, `chr`, `to_string` |
| Numbers | `to_int`, `to_float`, `round`, `floor`, `ceil`, `abs`, `sqrt` |
| Files | `read_file(path)`, `write_file(path, text)`, `load(path)`, `load_error(path)` |
| Program | `args()`, `exit(code)`, `fail(message)`, `type_of(value)` |

Functions that change an array or map (`append`, `pop`, `insert`, `remove`, `clear`, `reverse`, `sort`) take it as a variable, e.g. `append(items, 4)`.

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
compiler/   interpreter in C (lexer, parser, interpreter, values, builtins,
            diagnostics) and data.c, the file-format parsers used by import
selfhost/   compiler written in Syger, plus the C runtime compiled programs use
scripts/    sygerc and the bootstrap test
tests/      semantics and feature tests, error tests
docs/       error reference, self-hosting notes
examples/   hello.sg (a tour of the language), import_demo.sg
```

How the self-hosting bootstrap works is described in [docs/SELF_HOSTING.md](docs/SELF_HOSTING.md).

## License

No license yet.
