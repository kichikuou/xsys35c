#!/bin/bash
#
# rtt.sh: Round trip testing
#
# This decompiles given ALD file, compiles the generated source code,
# and verifies it matches the original ALD.
#

if [ $# -eq 0 ]; then
	echo 'Usage: rtt.sh [options] <aldfile(s)> [<ainfile>]'
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
./compiler/xsys35c -p "$out"/xsys35c.cfg || Exit 1

for file in "$@"; do
	[[ "$file" == -* ]] && continue
	base=$(basename "$file")
	if [[ "$file" == *.ALD ]]; then
		./tools/ald compare "$file" "$out"/"$base" || Exit 1
	else
		cmp "$file" "$out"/"$base" || Exit 1
	fi
done

Exit 0
