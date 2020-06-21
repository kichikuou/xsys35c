#!/bin/sh
# To update testdata/regression_test.ald, remove it and run this script
set -e

compiler/xsys35c -p testdata/source/xsys35c.cfg -o testdata/actual
if [ -f testdata/regression_test.ald ]; then
    ./tools/ald compare testdata/regression_test.ald testdata/actualSA.ALD
else
    cp testdata/actualSA.ALD testdata/regression_test.ald
fi
rm -rf testdata/decompiled
decompiler/xsys35dc -o testdata/decompiled testdata/actualSA.ALD
diff -uN --strip-trailing-cr testdata/source testdata/decompiled
