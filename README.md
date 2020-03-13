# xsys35c: System 3.x Compiler
xsys35c is a compiler for AliceSoft's System 3.x game engine.

This is still a work-in-progress project, but this can compile the System3.5-ported games that comes with the System 3.5 SDK (Rance 1-3, Toushin Toshi, DALK, and Dr.STOP). Generated binaries match the output of the official SDK.

Currently, xsys35c only supports System 3.5 features, but not System 3.6 / 3.8 / 3.9. This also means that game messages cannot include ASCII characters.

## Build
To build `xsys35c`, run `make` command:
```
make
```
Run `make test` to run unit tests.

## Usage
```
./xsys35c source_file... -o ald_file
```

For example, the following command will compile `examples/sample.adv` and generates `sample_sa.ald`:
```
./xsys35c examples/sample.adv -o sample_sa.ald
```

Run `xsys35c --help` to see full options.

## Character Encoding
Currently `xsys35c` assumes that:
- Source files are Shift_JIS encoding.
- Filesystem paths and console output are UTF-8 encoding. This could be problematic in Windows.

## Implementation notes

### Compiration process
`xsys35c` scans each source file twice. The first pass only collects information about variables and function definitions (names and parameters). The second pass directly generates bytecode while parsing the input; `xsys35c` does not use any intermediate representations such as AST.

### Memory management
No memory management is the memory management policy. `malloc()`ed memory regions are never freed until `xsys35c` terminates. This should be fine as `xsys35c` is a short-lived program, and it doesn't allocate a lot.
