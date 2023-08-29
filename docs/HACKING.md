# Implementation Notes

## Compilation Process
`xsys35c` scans each source file in two passes. The first pass collects information solely about variables and function definitions, including their names and parameters. The second pass generates bytecode directly while parsing the input; `xsys35c` does not use any intermediate representations, such as an AST.

## Memory Management
The memory management policy in `xsys35c` is to not explicitly free memory. Regions of memory allocated with `malloc()` are not freed until `xsys35c` terminates. This approach is generally acceptable because `xsys35c` is a short-lived program and does not allocate significant amounts of memory.