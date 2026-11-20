# Syger error reference

Every error Syger reports has a code. The interpreter, the native compiler, and compiled programs use the same codes and the same format:

```
error[E3001]: variable 'totl' is not defined
 --> report.sg:3
  |
3 | print(totl + count)
  |
  = help: did you mean 'total'?
```

Errors raised inside functions end with `note:` lines showing the chain of calls.

Codes are grouped by where the problem is found:

| Range | Stage |
| --- | --- |
| `E0xxx` | Starting up: files, memory |
| `E1xxx` | Reading characters (lexer) |
| `E2xxx` | Reading structure (parser) |
| `E3xxx` | Names, types, and values. The native compiler reports name and call errors before the program runs; everything else is reported when the line runs. |
| `E9xxx` | Bugs in Syger itself |

Every code a program can trigger has a test program in [`tests/errors/`](../tests/errors/) with its expected output (except E1001, which needs 64 nested blocks).

| Code | Meaning |
| --- | --- |
| [E0001](#e0001) | Source file could not be read |
| [E0003](#e0003) | Out of memory |
| [E0004](#e0004) | No source file given |
| [E1001](#e1001) | Indentation nested too deeply |
| [E1002](#e1002) | Inconsistent indentation |
| [E1003](#e1003) | Unterminated string |
| [E1004](#e1004) | Unexpected `!` |
| [E1005](#e1005) | Unexpected character |
| [E1006](#e1006) | Unknown escape in a string |
| [E1007](#e1007) | Invalid number |
| [E2001](#e2001) | Syntax error |
| [E3001](#e3001) | Undefined variable |
| [E3002](#e3002) | Undefined function |
| [E3003](#e3003) | Wrong number of arguments |
| [E3004](#e3004) | `break` or `continue` outside a loop |
| [E3005](#e3005) | `return` outside a function |
| [E3006](#e3006) | Function defined twice |
| [E3007](#e3007) | Index or range out of bounds |
| [E3008](#e3008) | Indexing something that is not an array or map |
| [E3009](#e3009) | Index is not an integer |
| [E3010](#e3010) | Array function needs a variable |
| [E3011](#e3011) | Wrong argument type |
| [E3012](#e3012) | Empty array |
| [E3013](#e3013) | Division by zero |
| [E3014](#e3014) | Modulo by zero |
| [E3015](#e3015) | Condition is not a boolean |
| [E3016](#e3016) | `not`, `and`, `or` need booleans |
| [E3017](#e3017) | Unary minus needs a number |
| [E3018](#e3018) | Operator used on the wrong types |
| [E3019](#e3019) | `chr` code out of range |
| [E3020](#e3020) | File not found |
| [E3021](#e3021) | File could not be read |
| [E3022](#e3022) | `fail()` called |
| [E3023](#e3023) | Key not found in map |
| [E3024](#e3024) | Map key is not a string or integer |
| [E3025](#e3025) | `for` over something that can't be looped over |
| [E3026](#e3026) | Invalid data in an imported file |
| [E3029](#e3029) | Text can't be converted to a number |
| [E3030](#e3030) | Number can't be used for this operation |
| [E9001](#e9001) | Internal error |

---

## Startup

### E0001

The `.sg` file passed to `Syger` doesn't exist or can't be read.

```
$ ./Syger mising.sg
error[E0001]: Could not open file: mising.sg
  = help: Check that the file exists and is readable.
```

Check the path. It is relative to the directory you run `Syger` from.

### E0003

Syger ran out of memory. Usually caused by a loop that keeps growing an array or string without stopping.

### E0004

`Syger` was run without a file.

```
$ ./Syger
error[E0004]: Expected a source file
  = help: Usage: Syger <file.sg> [program arguments...]
```

---

## Lexer

### E1001

Blocks are nested more than 63 levels deep. Move the inner part into a function.

### E1002

A line is indented by an amount that doesn't match any block it could belong to.

```syger
if true
    a = 1
  b = 2
```

```
error[E1002]: inconsistent indentation
 --> e1002_inconsistent_indentation.sg:3:1
```

Use the same indentation as the block the line belongs to: 4 spaces to stay inside the `if`, 0 to leave it.

### E1003

A string was opened with `"` and never closed. Strings can't span lines.

```syger
name = "Syger
```

### E1004

Syger has no `!` operator. Use `not` for negation and `!=` for "not equal".

```syger
if !done          # error
if not done       # ok
```

### E1005

A character that isn't part of Syger's syntax, such as `$`, `@` or `;`.

```syger
total = price $ 2
```

```
error[E1005]: unexpected character '$'
 --> e1005_unexpected_character.sg:2:15
  |
2 | total = price $ 2
  |               ^
```

### E1006

A backslash in a string followed by a character Syger doesn't know. The supported escapes are `\n`, `\t`, `\r`, `\"` and `\\`.

```syger
path = "C:\docs"        # error: \d
path = "C:\\docs"       # ok
```

### E1007

A number with letters stuck to it, like `3px` or `2nd`. Names can't start with a digit.

---

## Parser

### E2001

The code doesn't have a shape Syger understands. The message says what was expected and the caret points at where it went wrong.

```syger
print("big"
```

```
error[E2001]: Expected ')' after function arguments
 --> e2001_syntax_error.sg:3:16
  |
3 |     print("big"
  |                ^
```

Common messages:

| Message | Usual cause |
| --- | --- |
| `Expected ')' after function arguments` | Missing `)` or a missing `,` between arguments |
| `Expected an expression` | An operator with nothing after it, e.g. `x = 1 +` |
| `Expected an indented block` | `if`, `while` or `fun` line with no indented body under it |
| `Expected '=' or '(' after identifier` | A bare name on its own line, e.g. `total` |
| `Expected a statement` | A line starting with something that can't start a statement, e.g. `5 + 3` |

---

## Names and calls

### E3001

A variable is read before anything was assigned to it. If the name is close to one that exists, the help line suggests it.

```syger
total = 10
print(totl)
```

```
error[E3001]: variable 'totl' is not defined
  = help: did you mean 'total'?
```

Inside a function you can read globals, but not the variables of the function that called you. Pass them as parameters instead.

### E3002

A function is called that was never defined (and isn't a built-in).

```syger
print(lenght(items))
```

```
error[E3002]: function 'lenght' is not defined
  = help: did you mean 'length'?
```

### E3003

A function or built-in got the wrong number of arguments. The help line shows how it's defined.

```syger
fun add(a, b)
    return a + b

print(add(1))
```

```
error[E3003]: function 'add' expects 2 argument(s), got 1
  = help: 'add' is defined as add(a, b)
```

### E3004

`break` or `continue` used outside a `while` loop.

### E3005

`return` used at the top level of the program, outside any `fun`. To stop a program early, use `exit(0)`.

### E3006

Two functions have the same name. Syger has no overloading; give them different names.

```syger
fun area(w, h)
    return w * h

fun area(size)
    return size * size
```

---

## Arrays and indexing

### E3007

An index or range is outside the array or string.

```syger
items = [1, 2, 3]
print(items[3])
```

```
error[E3007]: index 3 is out of bounds for an array of length 3
  = help: valid indexes are 0 to 2
```

Indexes start at 0, so the last element of `items` is `items[length(items) - 1]`. The same code is used for `slice`, `substring`, `insert`, `remove` and `token_at`.

### E3008

`[]` used on something that isn't an array or map.

```syger
count = 5
print(count[0])
```

```
error[E3008]: indexing requires an array or map (got integer)
```

Strings can be indexed for reading (`"abc"[1]` is `"b"`) but not assigned to.

### E3009

An array index that isn't an integer. For string keys, use a map.

```syger
print(items["first"])
```

### E3010

`append`, `pop`, `insert`, `remove`, `clear`, `reverse` and `sort` change an array (or, for `remove`, a map) in place, so their first argument must be something that can be changed: a variable or an element like `rows[0]`.

```syger
append([1, 2], 3)      # error: nothing to store the result in

items = [1, 2]
append(items, 3)       # ok
```

### E3011

A built-in got a value of the wrong type.

```syger
print(length(42))
```

```
error[E3011]: length requires a string, array or map (got integer)
```

Also raised by `join` on non-strings (convert items with `to_string` first), `sort` on a mix of strings and numbers, `sum` on non-numbers, `keys` on a non-map, `range` with non-integers or a step of 0, and string functions like `split` or `upper` given something that isn't a string.

### E3012

`pop`, `first`, `last`, `min` or `max` on an empty array. (`sum([])` is 0.)

```syger
queue = []
next = pop(queue)
```

Check `length(queue) > 0` first.

---

## Operators and conditions

### E3013

Division by zero, for integers and floats alike.

```syger
print(total / count)    # count is 0
```

### E3014

`%` with zero on the right.

### E3015

`if` and `while` conditions must be `true` or `false`. Syger doesn't treat numbers, strings, arrays or maps as true or false.

```syger
if count            # error
if count > 0        # ok
```

### E3016

`not`, `and` and `or` only work on booleans.

```syger
print(not 5)
```

### E3017

Unary minus on something that isn't a number, e.g. `-"five"`.

### E3018

An operator used on types it doesn't support. `+` adds numbers, joins two strings, or joins two arrays, but can't mix them; `-`, `*`, `/` and `%` need numbers; `<`, `<=`, `>` and `>=` compare two numbers or two strings.

```syger
name = "Syger"
version = 2
print(name + version)
```

```
error[E3018]: cannot use '+' on string and integer
  = help: convert the other value first, e.g. "total: " + to_string(count)
```

`==` and `!=` work on any two values and never raise this error.

### E3019

`chr` only supports ASCII codes 0 to 127.

---

## Files

### E3020

A file couldn't be opened. For `import`, the path is relative to the `.sg` file doing the import, and the native compiler reports this when compiling. For `read_file` and `load`, it is relative to the directory you run the program from.

```
error[E3020]: could not open 'tests/errors/data/missing.json'
 --> e3020_import_missing.sg:1
  = help: import paths are relative to the .sg file that imports them
```

### E3021

The file exists but couldn't be read, for example because the path is a directory.

---

## From your program

### E3022

Your program called `fail(message)`. The error shows your message and the line that called `fail`.

```syger
if age < 0
    fail("age cannot be negative")
```

```
error[E3022]: age cannot be negative
 --> e3022_fail.sg:3
```

### E3023

A map doesn't have the key being read. The help line suggests a close key if there is one.

```syger
config = {"name": "demo", "port": 80}
print(config["nmae"])
```

```
error[E3023]: key 'nmae' not found in map
  = help: did you mean 'name'?
```

Check with `contains(map, key)` first, or use `get(map, key, default)`. Assigning (`config["nmae"] = 1`) adds the key instead.

### E3024

Map keys must be strings or integers.

### E3025

`for` was given something that isn't an array, map, string or `range(...)`.

```syger
for i in 5          # error
for i in range(5)   # ok
```

### E3026

An imported file isn't valid for its format. The message gives the line in the data file:

```
error[E3026]: invalid data in data/broken.json:4: expected ',' or '}' in object
 --> e3026_invalid_data.sg:1
```

With the native compiler this is reported at compile time. To check a file without stopping the program, use `load_error(path)`, which returns the message or `""`.

### E3029

`to_int` or `to_float` got text that isn't a number, like `to_int("42abc")`. `to_int` also refuses `"3.5"`; use `to_float` for that.

### E3030

A number that can't be used for the operation: `sqrt` of a negative number, or turning infinity or a huge float into an integer.

---

## Internal

### E9001

Something happened that shouldn't be possible, such as an unknown operator reaching the interpreter. This is a bug in Syger. Please open an issue with the program that triggered it.
