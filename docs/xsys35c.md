# xsys35c: System 3.x Compiler
`xsys35c` is a compiler for AliceSoft's System 3.x game engine.

## Usage
```
# Run a compile based on xsys35c.cfg in current directory
xsys35c

# Run a compile based on the configuration file
xsys35c --project some/dir/xsys35c.cfg

# Compile files specified in comp.hed
xsys35c --hed comp.hed

# Compile just the scenario.adv
xsys35c scenario.adv
```
If none of `--project`, `--hed` nor ADV files are specified, `xsys35c` uses `xsys35c.cfg` in current directory.

The following options are available:
<dl>
  <dt><code>-a <var>file</var></code>
  <br/><code>--ain <var>file</var></code></dt>
  <dd>Write <code>.ain</code> output to <var>file</var>. (default: <code>System39.ain</code>)</dd>

  <dt><code>-E <var>enc</var></code>
  <br/><code>--encoding <var>enc</var></code></dt>
  <dd>Specify text encoding of input files. Possible values are <code>sjis</code>, <code>utf8 (default)</code>.</dd>

  <dt><code>-i <var>file</var></code>
  <br/><code>--hed <var>file</var></code></dt>
  <dd>Read <a href="#compile-header-hed-file">compile header</a> (<code>.hed</code>) from <var>file</var>.</dd>

  <dt><code>-h</code>
  <br/><code>--help</code></dt>
  <dd>Display help message about <code>xsys35c</code> and exit.</dd>

  <dt><code>-o <var>file</var></code>
  <br/><code>--output <var>file</var></code></dt>
  <dd>Write <code>.ald</code> output to <var>file</var>. (default: <code>adisk.ald</code>)</dd>

  <dt><code>-p <var>file</var></code>
  <br/><code>--project <var>file</var></code></dt>
  <dd>Read <a href="#project-configuration-file-xsys35ccfg">project configuration</a> from <var>file</var>.</dd>

  <dt><code>-s <var>ver</var></code>
  <br/><code>--sys-ver <var>ver</var></code></dt>
  <dd>Sets target System version. Available values are <code>3.5</code>, <code>3.6</code>, <code>3.8</code> (default), <code>3.9</code>,</dd>

  <dt><code>-V <var>file</var></code>
  <br/><code>--variables <var>file</var></code></dt>
  <dd>Read list of variables from <var>file</var>.</dd>

  <dt><code>-v</code>
  <br/><code>--version</code></dt>
  <dd>Display the <code>xsys35c</code> version number and exit.</dd>
</dl>

## Project configuration file (`xsys35c.cfg`)
The project configuration file specifies [compile header](#compile-header-hed-file) file and the other options used to compile the project. Here is an example configuration file:
```
sys_ver = 3.9
hed = comp.hed
```
This instructs `xsys35c` to use the compile header `comp.hed` and compile for System 3.9.

Relative paths in configuration file are resolved based on the directory of the configuration file.

## Compile header (`.hed`) file
The compile header file specifies a list of [scenario source](#scenario-source-adv-file) files to compile. In System 3.9 games, it may also list dynamic link libraries used in the game.

This file format is also used in the official System 3.x SDK.

Here is an example compile header:
```
#SYSTEM35
start.adv
sub.adv

#DLLHeader
mydll.hel
dependency.dll
```
A compile header file starts with a `#SYSTEM35` line, followed by a list of scenario source files. The order in the list is important; execution starts from the beginning of the first scenario file.

Optionally, compile header file may have `#DLLHeader` section. It lists [DLL function declaration](#dll-function-declaration-hel-file) (`.hel`) file, or DLL file name if the functions in the DLL are not called from any scenario file (but are used indirectly by another DLL).

## Scenario source (`.adv`) file
System 3.x source files, in the same language that the official System 3.x SDK accepts.

Explaining the System 3.x language is beyond the scope of this document; please see the official SDK documentation. Another good way to learn about the language is to decompile an existing game with [`xsys35dc`](xsys35dc.md) and look at the generated source files.

## DLL function declaration (`.hel`) file
A `.hel` file contains a list of functions (and their argument types) exported by a DLL, in a format similar to a C function declaration. You probably won't need to modify this file, or create your own.

## Bugs
- Although `xsys35c` accepts source files in UTF-8 encoding, characters outside the Shift_JIS character set cannot be used, even in comments.
