#!/bin/bash
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


tmpfile=$(mktemp)

diff -u - <(tools/vsp -i testdata/*.vsp) <<EOF
testdata/16colors.vsp: 256x256, offset: (40, 20)
EOF
tools/vsp testdata/16colors.vsp -o $tmpfile && cmp testdata/16colors.png $tmpfile
tools/vsp -e testdata/16colors.png -o $tmpfile && cmp testdata/16colors.vsp $tmpfile

diff -u - <(tools/pms -i testdata/*.pms) <<EOF
testdata/256colors.pms: PMS 1, 256x256 8bpp, offset: (50, 30)
testdata/highcolor.pms: PMS 1, 256x256 16bpp, offset: (50, 30)
testdata/highcolor_alpha.pms: PMS 1, 256x256 16bpp, 8bit alpha
EOF
tools/pms testdata/256colors.pms -o $tmpfile && cmp testdata/256colors.png $tmpfile
tools/pms -e testdata/256colors.png -o $tmpfile && cmp testdata/256colors.pms $tmpfile
tools/pms testdata/highcolor_alpha.pms -o $tmpfile && cmp testdata/highcolor_alpha.png $tmpfile
tools/pms -e testdata/highcolor_alpha.png -o $tmpfile && cmp testdata/highcolor_alpha.pms $tmpfile

diff -u - <(tools/qnt -i testdata/*.qnt) <<EOF
testdata/truecolor.qnt: QNT 1, 256x256 24bpp, offset: (60, 40)
testdata/truecolor_alpha.qnt: QNT 1, 256x256 24bpp + alpha
EOF
tools/qnt testdata/truecolor.qnt -o $tmpfile && cmp testdata/truecolor.png $tmpfile
tools/qnt -e testdata/truecolor.png -o $tmpfile && cmp testdata/truecolor.qnt $tmpfile
tools/qnt testdata/truecolor_alpha.qnt -o $tmpfile && cmp testdata/truecolor_alpha.png $tmpfile
tools/qnt -e testdata/truecolor_alpha.png -o $tmpfile && cmp testdata/truecolor_alpha.qnt $tmpfile

rm $tmpfile
