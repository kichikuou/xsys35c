# xsys35dc: System 3.x Decompiler
`xsys35dc` is a decompiler for AliceSoft's System 3.x game engine. It reads a scenario file (usually with the suffix `SA.ALD`) and optionally a `System39.ain` file (for System 3.9 games), and generates source files that can be compiled with [`xsys35c`](xsys35c.md) or the official System 3.x SDK.

## Usage
```
xsys35dc [options] aldfile [ainfile]
```
The following options are available:
<dl>
  <dt><code>-h</code>
  <br/><code>--help</code></dt>
  <dd>Display help message about <code>xsys35dc</code> and exit.</dd>

  <dt><code>-o <var>directory</var></code>
  <br/><code>--outdir <var>directory</var></code></dt>
  <dd>Generate output files under <var>directory</var>. By default, output files are generated in current directory.</dd>

  <dt><code>-s</code>
  <br/><code>--seq</code></dt>
  <dd>Output ADV files with sequential filenames (<code>0.adv</code>, <code>1.adv</code>, ...) instead of their original names.</dd>

  <dt><code>-v</code>
  <br/><code>--version</code></dt>
  <dd>Display the <code>xsys35dc</code> version number and exit.</dd>
</dl>

In addition to System 3.x source files (`*.ADV`), `xsys35dc` also generates `xsys35c.cfg` file which is a configuration file for the `xsys35c` compiler. Using this, the original scenario file can be reconstructed by:
```
xsys35c -p xsys35c.cfg
```

If you want to use the official System 3.x SDK, specify the generated `xsys35dc.hed` file in the `DIR.HED` file of your project directory.

## Character Encoding
`xsys35dc` generates source files in Shift_JIS character encoding.

## Bugs
- Scenario files of some games cannot be decompiled. See [README.md](../README.md) for the list of such games.
- `xsys35dc` tries to extract strings from data area using heuristics, but this sometimes fails.
