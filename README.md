# Syger

**Syger** (pronounced *seh-ger*) is a programming language built from the ground up in C.

Syger aims to provide a simple, readable syntax while maintaining a solid foundation for further language development.

> **Syger source files use the `.sg` extension.**

## Example

~~~syger
print("Hello, Syger!")
print("This is Syger.")

name = "Syger"
print(name)
~~~

## Features

- Clean, readable syntax
- Variables
- Values and expressions
- `print()` for output
- Lexer
- Parser
- Abstract Syntax Tree (AST)
- Interpreter
- `.sg` source files

## Getting Started

Syger is currently distributed as source code. To use Syger, clone the repository and compile it with Clang.

### Requirements

- Git
- Clang
- A C17-compatible environment

### Clone the repository

~~~bash
git clone https://github.com/Reinforged/Syger
cd Syger
~~~

### Build Syger

~~~bash
clang -Wall -Wextra -std=c17 compiler/*.c -o Syger
~~~

### Run a Syger program

~~~bash
./syger examples/hello.sg
~~~

You can also run your own `.sg` files:

~~~bash
./syger path/to/program.sg
~~~

## Project Structure

~~~text
Syger/
├── compiler/
│   ├── main.c
│   ├── lexer.c
│   ├── parser.c
│   ├── ast.c
│   ├── interpreter.c
│   └── value.c
├── examples/
│   └── hello.sg
└── README.md
~~~

## Hello, Syger

The simplest Syger program is:

~~~syger
print("Hello, Syger!")
~~~

Save it as `hello.sg` and run:

~~~bash
./syger hello.sg
~~~

## Development

Syger is actively developed. Its syntax, runtime, and language features may evolve as development continues.

The `examples/hello.sg` program is kept up to date with the language and serves as a basic regression test.

Contributions, ideas, and feedback are welcome.

## License

License information will be added when the project is licensed.

