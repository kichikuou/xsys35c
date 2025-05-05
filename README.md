# System 3.x Compiler and Decompiler
This is an open-source toolchain for AliceSoft's System 3.x game engine.

The following games can be decompiled and then recompiled into an ALD file that
is identical to the original:

- Rance
- Rance II
- Rance III
- Rance IV
- 鬼畜王ランス
- Rance5D
- 闘神都市
- 闘神都市II
- Dr.STOP!
- DALK
- 学園KING
- 零式
- いけないかつみ先生
- かえるにょ・ぱにょ〜ん
- 戦巫女
- アトラク＝ナクア
- 王道勇者
- AmbivalenZ
- DiaboLiQuE
- ぱすてるチャイム
- ぷろすちゅーでんとGood
- HUSHABY BABY
- 守り神様
- ママトト
- DARCROWS
- PERSIOM
- SeeIn青
- 20世紀アリス (かえるにょ国にょアリス / これDPS?)
- 夜が来る!
- 大悪司
- 超昂天使エスカレイヤー
- Only You -リ・クルス-
- 妻みぐい
- 妻みぐい2
- シェル・クレイル
- 隠れ月
- 王子さまLv1
- 王子さまLv1.5
- 俺の下であがけ
- 楽園行
- Nor
- 翡翠の眸

For older games (games with `ADISK.DAT` file), check out
[sys3c](https://github.com/kichikuou/sys3c).

## Download
Prebuilt Windows executables are available
[here](https://github.com/kichikuou/xsys35c/releases). The 64-bit version
supports Windows 10 or later. For older Windows, please use the 32-bit version.

## Build from Source
First, install the required dependencies (with corresponding Debian packages in
parentheses):
- meson (`meson`)
- libpng (`libpng-dev`)
- asciidoctor (`asciidoctor`) [optional, for generating manual pages]

Then, build the executables using Meson:
```
meson setup build
ninja -C build
```
This will create the following executables in the `build` directory:
- `xsys35c` — System 3.x compiler
- `xsys35dc` — System 3.x decompiler
- `ald` — ALD archive utility
- `alk` — ALK archive utility
- `vsp` — VSP image utility
- `pms` — PMS image utility
- `qnt` — QNT image utility

Optionally, you can install these executables on your system by running:
```
sudo ninja -C build install
```
If you want to install them to a custom directory, specify the `--prefix=`
option when running Meson.

## Basic Workflow
Here are the steps for decompiling a game, editing its source code, and
recompiling it back to a scenario file. See
[docs/adv_language.adoc](docs/adv_language.adoc) for details on the language
used in the source files.

### Windows

1. Copy all files in the xsys35c archive to the game directory (the directory
   containing `.ALD` files).
2. Run `decompile.bat`. The decompiled source files will be generated in the
   `src` directory.
3. Edit the source files in the `src` directory as you like.
4. Run `compile.bat`. The scenario files in the game directory will be updated.

### Other Platforms

First, in the game directory (the directory containing `.ALD` files), run:
```
xsys35dc . --outdir=src
```

The decompiled source files will be generated in the `src` directory.
Edit them as you like.

Once you've finished editing, recompile the source files back to `.ALD` (and
`.ain` if applicable) using the following command:
```
xsys35c --project=src/xsys35c.cfg --outdir=.
```

Alternatively, you can use `xsys35c` and `xsys35dc` with
[Visual Studio Code](https://code.visualstudio.com/). For more information, see
[`vscode-system3x`](https://github.com/kichikuou/vscode-system3x).

## Unicode Mode
By using the "Unicode Mode" in xsys35c and
[xsystem35](https://github.com/kichikuou/xsystem35-sdl2) (an open-source
implementation of System 3.x), you can translate games into many languages not
supported by the original System 3.x. For more details, see
[docs/unicode.adoc](docs/unicode.adoc).

## Command Documentation
Here are the manuals for the commands:
- [docs/xsys35c.adoc](docs/xsys35c.adoc)
- [docs/xsys35dc.adoc](docs/xsys35dc.adoc)
- [docs/ald.adoc](docs/ald.adoc)
- [docs/alk.adoc](docs/alk.adoc)
- [docs/vsp.adoc](docs/vsp.adoc) (also for `pms` and `qnt` commands)
