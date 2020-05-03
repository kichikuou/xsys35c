# System 3.x Compiler and Decompiler
This is an open-source toolchain for AliceSoft's System 3.x game engine.

Currently, the following games can be decompiled and compiled (and the compiled files match the original):
- Rance
- Rance III
- Rance IV
- 鬼畜王ランス
- Rance5D
- 闘神都市
- Dr.STOP!
- DALK
- 零式
- いけないかつみ先生
- 戦巫女
- AmbivalenZ
- DiaboLiQuE
- ぱすてるチャイム
- ぷろすちゅーでんとGood
- HUSHABY BABY
- 守り神様
- ママトト
- DARCROWS
- 隠れ月
- SeeIn青
- 大悪司
- 超昂天使エスカレイヤー
- Only You -リ・クルス-

The following games cannot be (de)compiled yet.
- Rance II
- 闘神都市II
- アトラク＝ナクア
- 王道勇者
- かえるにょ・ぱにょ〜ん
- PERSIOM
- 夜が来る!

## Build & Install
To build the executables, run `make` command:
```
make
```
This creates three executables, `compiler/xsys35c`, `decompiler/xsys35dc` and `tools/ald`.

`make install` will install them to `/usr/local/bin`:
```
sudo make install
```
If you want to install them in a custom directory, specify `PREFIX=`. For example, this will install the commands under `$HOME/bin`:
```
PREFIX=$HOME make install
```

## Basic Workflow
Here are the steps for decompiling a game, editing the source, and compiling back to the scenario file.

First, decompile the game by giving the scenario file (`*SA.ALD`) to `xsys35dc`.
```
xsys35dc -o decompiled fooSA.ALD
```
If the game has a `System39.ain` file, provide it too to the command line:
```
xsys35dc -o decompiled fooSA.ALD System39.ain
```

The decompiled source files will be generated in the `decompiled` directory. Edit them as you like.

Once you've finished editing the source files, you can compile them back to the .ALD (and .ain if any) using the following command:
```
xsys35c -p decompiled/xsys35c.cfg -o out.ALD -a out.ain
```

## Command Documentations
Here are the manuals for the commands:
- [docs/xsys35c.md](docs/xsys35c.md)
- [docs/xsys35dc.md](docs/xsys35dc.md)
- [docs/ald.md](docs/ald.md)
