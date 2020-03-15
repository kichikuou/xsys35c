#!/bin/sh
set -e

./xsys35c --timestamp 0 -s 3.8 testdata/cmd2f.adv -o testdata/cmd2f_actual.ald
cmp testdata/cmd2f.ald testdata/cmd2f_actual.ald
rm testdata/cmd2f_actual.ald
