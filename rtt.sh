#!/bin/bash
#
# rtt.sh: Round trip testing
#
# This decompiles given ALD file, compiles the generated source code,
# and verifies it matches the original ALD.
#

if [ $# -eq 0 ]; then
	echo 'Usage: rtt.sh [options] <aldfile> [<ainfile>]'
	echo 'Options:'
	echo '    -o <directory>  Generate outputs into <directory>'
	exit 1
fi

if [ "$1" = "-o" ]; then
	out="$2"
	shift 2
	cleanup=
else
	out=$(mktemp -d)
	cleanup="rm -rf $out"
fi

Exit() {
	$cleanup
	exit $1
}

./decompiler/xsys35dc -a -o "$out" "$@" || Exit 1
./compiler/xsys35c -p "$out"/xsys35c.cfg -o "$out"/out.ald -a "$out"/out.ain || Exit 1
./tools/ald compare "$1" "$out"/out.ald || Exit 1
if [ -n "$2" ]; then
	cmp "$2" "$out"/out.ain || Exit 1
fi
Exit 0
