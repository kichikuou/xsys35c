# ald: ALD archive utility
## Usage
```
ald <command> [<args>]
```
The following commands are available:

### `list`
Usage:
```
ald list <aldfile>...
```
`ald list` lists files in the ALD archive. Since an ALD archive may consist of multiple files (`xxxA.ALD`, `xxxB.ALD`, ...), multiple ALD file can be specified.

The following information are displayed for each file in the archive:
- index
- volume id (1 for `xxxA.ALD`, 2 for `xxxB.ALD`, etc.)
- timestamp
- size
- filename

### `extract`
Usage:
```
ald extract <aldfile>
```
`ald extract` extracts files from an ALD archive. It extracts all files in the archive into current directory.

### `dump`
Usage:
```
ald dump <aldfile> [<index>|<filename>]
```
`ald dump` displays content of a file in the ALD archive, specified by an index or a file name, in hexadecimal bytes + SJIS-aware characters format.

### `compare`
Usage:
```
ald compare <aldfile1> <aldfile2>
```
`ald compare` compares contents of two ALD archives, ignoring timestamp differences and upper/lower case differences in file names.

Exit status is 0 if the two archives are equivalent, 1 if different.

### `help`
Usage:
```
ald help <command>
```
`ald help` displays help information about *command* and exits.
