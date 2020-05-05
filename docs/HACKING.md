# Implementation notes

## Compilation process
`xsys35c` scans each source file twice. The first pass only collects information about variables and function definitions (names and parameters). The second pass directly generates bytecode while parsing the input; `xsys35c` does not use any intermediate representations such as AST.

## Memory management
No memory management is the memory management policy. `malloc()`ed memory regions are never freed until `xsys35c` terminates. This should be fine as `xsys35c` is a short-lived program, and it doesn't allocate a lot.
