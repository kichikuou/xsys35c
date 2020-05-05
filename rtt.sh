#!/bin/bash
#
# rtt.sh: Round trip testing
#
# This decompiles given ALD file, compiles the generated source code,
# and verifies it matches the original ALD.
#
# Usage: rtt.sh aldfile [ainfile]
#
set -e

rm -rf rtt_adv
./decompiler/xsys35dc -a -o rtt_adv $1 $2
./compiler/xsys35c -p rtt_adv/xsys35c.cfg -o rtt_adv/out.ald -a rtt_adv/out.ain
./tools/ald compare $1 rtt_adv/out.ald
if [ -n "$2" ]; then
	cmp $2 rtt_adv/out.ain
fi
