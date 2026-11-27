#!/bin/sh
# Set the PEF loader header's mainSection to -1 and mainOffset to 0 ("no main
# symbol"); MakePEF writes 1 / 0xFFFFFFFF. Plain POSIX tools: no python needed.
# usage: patch-pef-main.sh <file.pef>
set -e
f=$1
u32() { od -An -tu1 -j "$2" -N4 "$1" | awk '{print $1*16777216+$2*65536+$3*256+$4}'; }
n=$(od -An -tu1 -j32 -N2 "$f" | awk '{print $1*256+$2}')
loader=
i=0
while [ "$i" -lt "$n" ]; do
  h=$((40 + 28 * i))
  kind=$(od -An -tu1 -j$((h + 24)) -N1 "$f" | tr -d ' ')
  if [ "$kind" = 4 ]; then loader=$(u32 "$f" $((h + 20))); fi
  i=$((i + 1))
done
[ -n "$loader" ] || { echo "patch-pef-main: no loader section" >&2; exit 1; }
printf '\377\377\377\377\0\0\0\0' | dd of="$f" bs=1 seek="$loader" conv=notrunc 2>/dev/null
echo "patch-pef-main: loader at $loader: mainSection -1, mainOffset 0"
