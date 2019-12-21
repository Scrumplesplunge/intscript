# IntCode Assembler

This is an assembler for IntCode, a machine code described in the [2019 edition
of Advent of Code](https://adventofcode.com/2019).

## Compilation

I'm using the half-finished implementation of C++20 modules for this project, so
you'll need a fairly recent version of clang to compile this. If you don't have
such a compiler, you can get one using my
[get\_clang.sh](https://gist.github.com/Scrumplesplunge/bf8b92546e18c7f809b2e8f296f86368)
script. After that, you can change the value of the `CLANG_PREFIX` variable in
the Makefile to point at the newly compiled version.

## Usage

The assembler supports the following usage:

    # Specify input and output files.
    bin/debug/as --input examples/hello_world.asm --output hello_world.ic

    # Read from stdin and write to stdout.
    bin/debug/as <examples/hello_world.asm >hello_world.ic

    # Run the assembled program.
    bin/debug/run hello_world.ic

## Code Layout

  * `as/ast.cc` - The abstract syntax tree (AST) of the assembly code.
  * `as/parser.cc` - Code which reads the text representation and produces AST.
  * `as/encode.cc` - Code which takes AST and dumps out IntCode machine code.
