# Syger

Syger (pronounced *seh-ger*) is a small programming language written in C.
Programs can be run directly by the interpreter or compiled to native executables by Syger's self-hosted compiler (written in Syger itself).
It is designed around a simple, readable syntax with Python-like indentation, while keeping the implementation small and easy to understand.

Syger source files use the `.sg` extension.

**Status:** Syger is an actively developed language. The syntax and runtime may change as the project evolves.

---

## Features

Syger currently supports:

* Variables and reassignment
* Integer values
* Strings
* Booleans: `true` and `false`
* `none`
* Arrays, including nested arrays
* Array indexing and mutation
* Arithmetic operators
* Comparison operators
* Boolean operators: `and`, `or`, and `not`
* Operator precedence and parentheses
* `if`, `elif`, and `else`
* `while` loops
* `break` and `continue`
* User-defined functions
* Function parameters
* Multiple function parameters
* Return values
* Early returns
* Local function/block scope
* Function calls inside expressions
* Nested function calls
* Built-in array and string utilities
* Indentation-based blocks
* `#` comments
* Lexical scoping (functions see their own locals and the globals)
* Value semantics for arrays (assigning or passing an array behaves like a copy)
* A lexer, parser, AST, and tree-walking interpreter
* A self-hosted compiler that turns `.sg` programs into native executables via C

---

## Example

```syger
name = "Syger"

if name == "Syger"
    print("Hello, " + name)
else
    print("Unknown language")
```

Syger uses indentation to define blocks, so braces are not required.

---

## Getting Started

### Requirements

You need:

* Git
* Clang
* A C17-compatible C environment

No external runtime or package manager is required.

### Clone

```bash
git clone https://github.com/Reinforged/Syger
cd Syger
```

### Build

Compile the interpreter with Clang:

```bash
clang -Wall -Wextra -std=c17 compiler/*.c -o Syger
```

This creates the `Syger` executable.

### Run a program

```bash
./Syger examples/hello.sg
```

You can also run your own Syger source file:

```bash
./Syger path/to/program.sg
```

The executable expects exactly one `.sg` source file.

---

## Language Basics

### Variables

Variables are created by assigning a value:

```syger
number = 42
name = "Syger"
enabled = true

print(number)
print(name)
print(enabled)
```

Variables can be reassigned:

```syger
value = 10
value = 20
value = value + 5
```

### Values

Syger currently supports these runtime value types:

```syger
number = 42
text = "Hello"
enabled = true
disabled = false
nothing = none
numbers = [1, 2, 3]
mixed = [42, "hello", true]
```

Arrays can contain other arrays:

```syger
matrix = [[1, 2], [3, 4]]

print(matrix[0][0])
print(matrix[1][1])
```

### Strings

Strings use double quotes:

```syger
name = "World"
message = "Hello, " + name

print(message)
print("hello" == "hello")
print("hello" != "world")
```

Strings can be concatenated with `+`.

### Arithmetic

Syger supports:

```syger
print(10 + 5)
print(10 - 5)
print(10 * 5)
print(10 / 5)
print(10 % 3)
```

Parentheses can be used to control precedence:

```syger
result = 10 + 5 * 2
print(result)

result = (10 + 5) * 2
print(result)
```

### Comparisons

The following comparison operators are supported:

* `==`
* `!=`
* `<`
* `<=`
* `>`
* `>=`

Example:

```syger
x = 10

print(x == 10)
print(x != 5)
print(x > 5)
print(x >= 10)
print(x < 20)
print(x <= 10)
```

### Boolean Operators

Syger supports:

```syger
print(true and true)
print(true and false)

print(true or false)
print(false or true)

print(not true)
print(not false)
```

`and` and `or` operate on boolean values, while `not` negates a boolean value.

### Conditionals

Syger uses indentation for conditional blocks.

#### `if`

```syger
if true
    print("The condition is true")
```

#### `if` / `else`

```syger
value = 10

if value > 5
    print("greater than five")
else
    print("five or less")
```

#### `elif`

```syger
value = 15

if value > 20
    print("greater than 20")
elif value > 10
    print("greater than 10")
elif value > 5
    print("greater than 5")
else
    print("five or less")
```

Conditions must evaluate to a boolean.

### Loops

#### `while`

```syger
counter = 0

while counter < 5
    print(counter)
    counter = counter + 1
```

#### `break`

```syger
counter = 0

while counter < 10
    counter = counter + 1

    if counter == 6
        break

    print(counter)
```

#### `continue`

```syger
counter = 0

while counter < 5
    counter = counter + 1

    if counter == 3
        continue

    print(counter)
```

`break` exits the current loop and `continue` skips to the next iteration.

### Functions

Functions can be defined with `fun`.

```syger
fun greet
    print("Hello from Syger!")

greet()
```

Functions can take parameters:

```syger
fun add(a, b)
    return a + b

print(add(10, 20))
```

Return values can be used directly in expressions:

```syger
fun double(x)
    return x * 2

result = double(10) + 5
print(result)
```

Functions can also call other functions:

```syger
fun double(x)
    return x * 2

fun quadruple(x)
    return double(double(x))

print(quadruple(5))
```

Functions support multiple parameters, local variables, conditional returns, and early returns.

#### Function Declaration Syntax

The lexer currently accepts `fun`, `def`, and `function` as function-declaration keywords:

```syger
fun add(a, b)
    return a + b
```

The `fun` form is used by the project's examples.

### Arrays

Arrays are written using square brackets:

```syger
numbers = [10, 20, 30, 40]

print(numbers)
print(numbers[0])
print(numbers[2])
```

Arrays can contain different value types:

```syger
values = [42, "hello", true]
```

Nested arrays are supported:

```syger
matrix = [[1, 2], [3, 4]]

print(matrix[0][1])
print(matrix[1][0])
```

Array elements can be reassigned:

```syger
numbers = [10, 20, 30]

numbers[1] = 99

print(numbers)
```

Nested indexing and mutation are supported:

```syger
matrix[1][0] = 77
```

Array indexes must be non-negative integers within the array bounds.

---

## Built-in Functions

Syger currently provides the following built-in functions.

### Output

`print(...)`

Print one or more values:

```syger
print("Hello")
print(42)
print("value:", 42)
```

`print` accepts multiple arguments and prints each one on its own line.

### General / Array Utilities

| Function | Description |
| --- | --- |
| `length(value)` | Returns the length of a string or array |
| `first(array)` | Returns the first element |
| `last(array)` | Returns the last element |
| `contains(array, value)` | Checks whether an array contains a value |
| `index_of(array, value)` | Returns the first matching index, or -1 |
| `append(array, value)` | Adds a value to the end of an array |
| `pop(array)` | Removes and returns the last element |
| `insert(array, index, value)` | Inserts a value at an index |
| `remove(array, index)` | Removes and returns an element |
| `clear(array)` | Removes all elements |
| `reverse(array)` | Reverses an array in place |
| `sort(array)` | Sorts an integer array |
| `slice(array, start, end)` | Returns a portion of an array |
| `join(array, separator)` | Joins array values into a string |
| `sum(array)` | Returns the sum of an integer array |
| `min(array)` | Returns the minimum integer |
| `max(array)` | Returns the maximum integer |

For example:

```syger
values = [4, 1, 3, 2]

print(length(values))
print(first(values))
print(last(values))
print(contains(values, 3))
print(index_of(values, 3))

append(values, 5)
print(values)

sort(values)
print(values)

reverse(values)
print(values)

print(sum(values))
print(min(values))
print(max(values))
```

`sort`, `sum`, `min`, and `max` currently operate on arrays of integers. `first` and `last` require a non-empty array.

### Strings, Types, Files, and Program Arguments

| Function | Description |
| --- | --- |
| `char_at(text, index)` | Returns the character at an index, or `""` when out of range |
| `substring(text, start, end)` | Returns the characters from `start` up to (not including) `end` |
| `chr(code)` | Returns the one-character string for an ASCII code (0–127) |
| `to_string(value)` | Converts a value to a string |
| `type_of(value)` | Returns `"string"`, `"integer"`, `"boolean"`, `"array"`, or `"none"` |
| `token_at(array, index)` | Returns an array element (same as `array[index]`) |
| `read_file(path)` | Returns the contents of a file as a string |
| `write_file(path, text)` | Writes a string to a file and returns `true` on success |
| `args()` | Returns the program's command-line arguments as an array of strings |
| `fail(message)` | Stops the program with an error message |

---

## Scope and Values

Syger is lexically scoped. A function can read and change its own parameters and local variables, and it can read the program's global variables (variables assigned at the top level of the file). Assigning to a name inside a function creates a local variable, even if a global with the same name exists:

```syger
total = 10

fun reset()
    total = 0        # a new local; the global is unchanged
    return total

print(reset())       # 0
print(total)         # 10
```

Functions can still update global arrays in place, because indexing and the array utilities change the existing value instead of assigning a new one:

```syger
counter = [0]

fun bump()
    counter[0] = counter[0] + 1

bump()
print(counter)       # [1]
```

Arrays behave like values. Assigning an array or passing it to a function gives the receiver its own copy, so changes do not leak back:

```syger
a = [1, 2]
b = a
append(b, 3)
print(a)             # [1, 2]
print(b)             # [1, 2, 3]
```

Internally these copies are cheap: strings and arrays are shared and reference counted, and an array is only actually copied when it is changed while shared (copy-on-write).

`==` and `!=` work on any two values. Values of different types are never equal, and arrays compare element by element.

---

## Indentation

Syger uses indentation to represent nested blocks.

For example:

```syger
if true
    print("inside the block")

    if false
        print("never reached")

    print("back in the outer block")
```

The lexer tracks indentation and emits `INDENT` and `DEDENT` tokens. Inconsistent indentation is reported as a lexer error.

---

## Implementation

Syger has two ways to run a program:

```
Interpreter:  Source (.sg) -> Lexer -> Parser -> AST -> Interpreter -> Output
Compiler:     Source (.sg) -> selfhost/compiler.sg -> C -> Clang -> native executable
```

* **Lexer:** `compiler/lexer.c` converts source text into tokens, including identifiers, literals, operators, keywords, indentation, and punctuation.
* **Parser:** `compiler/parser.c` converts tokens into an abstract syntax tree and handles expressions, arrays, indexing, assignments, conditionals, loops, functions, returns, `break`, and `continue`.
* **AST:** `compiler/ast.c` contains the AST representation and memory-management logic. Identifier names are interned so the interpreter can compare them by pointer.
* **Interpreter:** `compiler/interpreter.c` evaluates expressions, executes statements, manages environments and functions, and implements the built-in functions. Environments are recycled instead of allocated per call, and common expressions take a fast path.
* **Values:** `compiler/value.c` implements runtime values. Strings and arrays are reference counted and copy-on-write, and strings store their length.
* **Entry Point:** `compiler/main.c` loads a `.sg` file, initializes the lexer and parser, builds the AST, and passes it to the interpreter.
* **Self-hosted compiler:** `selfhost/compiler.sg` is written in Syger. It parses a program, resolves every variable at compile time, and emits C that calls functions and built-ins directly.
* **Native runtime:** `selfhost/native_runtime.c` and `.h` provide the values, operators, and built-ins used by compiled programs.

---

## Project Structure

```
Syger/
├── compiler/
│   ├── main.c
│   ├── lexer.c
│   ├── lexer.h
│   ├── parser.c
│   ├── parser.h
│   ├── ast.c
│   ├── ast.h
│   ├── interpreter.c
│   ├── interpreter.h
│   ├── value.c
│   ├── value.h
│   ├── diagnostics.c
│   └── diagnostics.h
├── selfhost/
│   ├── compiler.sg
│   ├── native_runtime.c
│   └── native_runtime.h
├── scripts/
│   ├── sygerc
│   └── bootstrap-selfhost.sh
├── tests/
│   └── selfhost/
├── docs/
│   └── SELF_HOSTING.md
├── examples/
│   └── hello.sg
└── README.md
```

The `examples/hello.sg` file exercises variables, values, strings, arrays, arithmetic, comparisons, logical operators, conditionals, loops, functions, returns, scoping, and combinations of these features.

---

## Running the Example Suite

After building Syger:

```bash
./Syger examples/hello.sg
```

The example program contains the project's broad language feature test suite and ends with:

```
=== ALL TESTS COMPLETE ===
```

This makes it useful as a quick sanity check after modifying the compiler or interpreter.

---

## Native Compilation and Self-Hosting

Syger includes a compiler written in Syger that translates `.sg` programs to C, then uses Clang to produce a native executable. The C interpreter is the stage0 bootstrap that runs the compiler for the first time.

Compile an individual Syger program to a native executable:

```bash
./scripts/sygerc examples/hello.sg -o build/hello
./build/hello
```

Set `SYGER_KEEP_C=1` to keep the generated C file; `sygerc` prints where it is.

Run the full stage0 → stage1 → stage2 → stage3 bootstrap check, which also compares interpreter and native output for the test programs:

```bash
./scripts/bootstrap-selfhost.sh
```

The generated stage2 and stage3 compiler C files must be byte-for-byte identical. Details and the bootstrap architecture are documented in [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md).

---

## Development

Syger is actively developed and its language features may change over time.

When adding or changing language functionality, run `./Syger examples/hello.sg` for a quick check and `./scripts/bootstrap-selfhost.sh` for the full test: it builds the self-hosted compiler and checks that the interpreter and compiled programs produce identical output, including for `tests/selfhost/semantics.sg`.

Contributions, ideas, bug reports, and feedback are welcome.

---

## License

Syger does not currently specify a license.
