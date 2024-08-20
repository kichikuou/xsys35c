# Changelog

## 1.11.0 - 2024-08-20
- `ald`: Added `dump-index` subcommand which is useful for inspecting malformed ALD archives.
- Fixed character encoding issues when non-ASCII output directory name is specified with `xsys35dc -o` or `ald extract -d`.

## 1.10.0 - 2023-11-21
Now the 64-bit version supports Windows 10 or later. For older Windows, please use the 32-bit version.
- Compiler: Fixed crash when the last line of ADV was a comment without a newline. (#8)
- Decompiler: Print message and subsequent `R`/`A` command on the same line.

## 1.9.2 - 2023-02-12
- Fixed character encoding issues with file names.

## 1.9.1 - 2023-01-26
- Added support for `SV` command used in Rance4 ver2.05.

## 1.9.0 - 2023-01-22
- Compiler: Added `--init` option which creates a new xsys35c project in current directory.
- Compiler, Decompiler: Added support for `LXX` command.
- `ald`: Support NL5-style manifest format.
- `ald`, `alk`: Fixed a bug when reading / writing 0-byte entries.

## 1.8.1 - 2022-01-26
- `ald list` no longer prints lines for missing entries.
- Compiler: Added warning for control characters in strings / messages.
- Decompiler: Allowed tab characters in strings. (https://github.com/kichikuou/xsystem35-sdl2/issues/29)
- Decompiler: Fixed decompilation error in Ouji-sama Lv1.
- Decompiler: Now you don't have to exclude dummy ALD files (e.g. `NIGHTSB.ALD` in Yoru ga Kuru!) from command line arguments.

## 1.8.0 - 2021-10-31
- Compiler: For [Unicode mode](https://github.com/kichikuou/xsys35c/blob/v1.8.0/docs/unicode.adoc), you no longer have to add a `ZU` command. (It is now automatically added by the compiler.)
- Compiler: Generates output files in the same directory as `xsys35c.cfg` by default.
- Decompiler: Removed `--unicode` option. Unicode games can be just decompiled without an option.
- Decompiler: Fixed an encoding issue when decompiling Unicode games.
- Improved error detection and error messages.

## 1.7.0 - 2021-10-23
- Now `xsys35c`/`xsys35dc` can be used from Visual Studio Code. See [vscode-system3x](https://github.com/kichikuou/vscode-system3x) for details.
- `pms`: Supported the old PMS format (also known as VSP256) used in System2-System3 games.
- Compiler: Fixed compilation of character references (`<0x####>`) in Unicode mode.
- Backslash (`\`) is now recognized as a path separator on Windows.

## 1.6.0 - 2021-08-07
- Compiler: Added `-g` option for generating debug information for xsystem35-sdl2.
- Compiler: Fixed a bug where `xsys35c -u` generated ALD with wrong filename character encoding.
- Decompiler: Added `-u` option for decompiling ALD with Unicode mode.
- `qnt`: Supported alpha-only images.

## 1.5.0 - 2021-03-14
- Added `alk` archive utility.
- Now `ald` can create multi-volume ALD archives using manifest files.
- `xsys35dc`: Added `--aindump` option.
- `pms`: Supported PMS version 2.

## 1.4.0 - 2020-09-27
- Added image manipulation tools, `vsp`, `pms` and `qnt`.

## 1.3.0 - 2020-09-13
- Now windows binary packages include manuals in the `docs/` folder.
- Added `ald create` command.
- `ald extract` now restores timestamps.
- `ald` command now uses index beginning from 1, instead of 0.
- Fixed a bug where `ald` command couldn't handle some ALD files created with unofficial tools.

## 1.2.0 - 2020-08-09
- Changed the compiler's internal character encoding to UTF-8.
- Added [Unicode mode](https://github.com/kichikuou/xsys35c/blob/v1.2.0/docs/unicode.md).
- Fixed `H`, `HH`, and `X` commands in System 3.9.

## 1.1.0 - 2020-07-12
- Decompiler: generates more readable function labels.
- Compiler: updated default target System version to 3.9.
- Fixed a problem with character encoding of file names on Windows.

## 1.0.0 - 2020-07-05
- First public release.
