# A small cross-compiler for a subset of C

Origin: [Puny C](https://github.com/bobbl/punycc)

Features
  * Supported target and host architectures: **RISC-V**.
  * Valid source code for Puny C is also valid C99 and can be written in a way
    that gcc or clang compile it without any warning.
  * Code generation is designed to be easily portable to other target
    architectures.
  * Fast compilation, small code size.

Inspired by
  * [cc500](https://github.com/8l/cc500) -
    a tiny self-hosting C compiler by Edmund Grimley Evans
  * [Obfuscated Tiny C Compiler](https://bellard.org/otcc/) -
    very small self compiling C compiler by Fabrice Bellard
  * [Tiny C Compiler](https://savannah.nongnu.org/projects/tinycc) -
    a small but hyper fast C compiler.
  * [Compiler Construction](https://people.inf.ethz.ch/wirth/CompilerConstruction/index.html) -
    brief but comprehensive book by Niklaus Wirth.

Run the following command under top-level directory.
```shell
tests/cc-selfhost.sh
```

## Language restrictions

  * No linker.
  * No preprocessor.
  * No standard library.
  * No `typedef`.
  * No type checking. Variable types are always `unsigned int`, except if
    indexed with `[]` then the type is `char *`.
  * Any combination of `unsigned`, `long` `int`, `char`, `void` and `*` is
    accepted as valid type.
  * Type casts are allowed, but ignored.
  * Constants: only decimal, character and string without backslash escape
  * Statements: `if`, `while`, `return`.
  * Variable declaration: C99-style statements.
  * Operators: no unary, ternary, extended assignment.
  * Operator precedence: simplified, use parentheses instead.

| level | operator             | description             |
| ----- | -------------------- | ----------------------- |
|   1   | [] ()                | array and function call |
|   2   | + - << >> & ^ &#124; | binary operation        |
|   3   | < <= > >= == !=      | comparison              |
|   4   | =                    | assignment              |

## Low-Level Functions

There is no inline assembler for functions that directly access the operating
system (e.g. file I/O). But code can be written in pure binary:

    void exit(int) _Pragma("emit \x58\x5b\x31\xc0\x40\xcd\x80");
    /*  58      pop eax
        5b      pop ebx
        31 c0   xor eax, eax
        40      inc eax
        cd 80   int 128 */

Other compilers ignore the `_Pragma` statement, which turns the line into a
forward declaration where libc can be linked against.

## Implementation Details

Each compiler consists of three parts:

 1. Host-specific standard functions for I/O in `stdlib.c`
 2. Target-specific code generation in `emit.c`
 3. Architecture independent compiler parts (scanner, parser and symbol table)

Concatenate the three files and compile it.
Cross compilers can be built by using a different `ARCH` for `host_` and `emit_`.

### Memory Management

There is only one buffer `buf`.
The code grows from 0 upwards, the symbol table grows from the top downwards.
The token buffer for strings and identifiers is dynamically allocated in the
space between them:

    0   code_pos     code_pos+256         sym_head-256      sym_head   buf_size
                       token_buf      token_buf+token_size
    +------+---------------+-------------------+---------------+--------------+
    | code |   256 bytes   | identifier/string |   256 bytes   | symbol table |
    +------+---------------+-------------------+---------------+--------------+
