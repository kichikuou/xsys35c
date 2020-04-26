#!/bin/sh
# To update testdata/regression_test.ald, remove it and run this script
set -e

compiler/xsys35c --timestamp 0 -p testdata/source/xsys35c.cfg -o testdata/actual.ald
if [ -f testdata/regression_test.ald ]; then
    ./tools/ald compare testdata/regression_test.ald testdata/actual.ald
else
    cp testdata/actual.ald testdata/regression_test.ald
fi
rm -rf testdata/decompiled
decompiler/xsys35dc -o testdata/decompiled testdata/actual.ald
diff -uN testdata/source testdata/decompiled
