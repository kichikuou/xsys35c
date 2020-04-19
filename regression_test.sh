#!/bin/sh
set -e

compiler/xsys35c --timestamp 0 -p testdata/source/xsys35c.cfg -o testdata/actual.ald
cmp testdata/regression_test.ald testdata/actual.ald
rm -rf testdata/decompiled
mkdir -p testdata/decompiled
decompiler/xsys35dc -o testdata/decompiled testdata/actual.ald
diff -uN testdata/source testdata/decompiled
