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
- 隠れ月
- SeeIn青
- 20世紀アリス (かえるにょ国にょアリス / これDPS?)
- 夜が来る!
- 王子さまLv1
- 王子さまLv1.5
- 大悪司
- 超昂天使エスカレイヤー
- Only You -リ・クルス-
- 妻みぐい
- 妻みぐい2
- シェル・クレイル

## Download
Prebuilt Windows executables are available
[here](https://github.com/kichikuou/xsys35c/releases). The 64-bit version
supports Windows 10 or later. For older Windows, please use the 32-bit version.

## Build & Install
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
recompiling it back to a scenario file.

First, decompile the game by specifying the scenario file (`*SA.ALD`) as an
argument for `xsys35dc`:
```
xsys35dc -o decompiled fooSA.ALD
```
If the game includes a `System39.ain` file, provide that as well:
```
xsys35dc -o decompiled fooSA.ALD System39.ain
```

The decompiled source files will be generated in the `decompiled` directory.
Edit them as you like.

Once you've finished editing, recompile the source files back to `.ALD` (and
`.ain` if applicable) using the following command:
```
xsys35c -p decompiled/xsys35c.cfg -o foo -a System39.ain
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
