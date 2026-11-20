#!/usr/bin/env bash
# Copy the RISC-V MESS sources (BSC, SiFive HiFive Unmatched port) into the build
# tree and apply the minimal edits needed to compile them bare-metal with gcc (C).
# usage: import_mess.sh <mess/src dir> <output dir> <loop accesses>
set -euo pipefail
SRC=$1; OUT=$2; LOOP=$3
mkdir -p "$OUT"
cp "$SRC/stream_mpi/src/utils.c" "$SRC/stream_mpi/src/utils.h" "$SRC/stream_mpi/src/nop.c" \
   "$SRC/ptr_chase/loop_template.h" "$OUT/"
sed -i 's/\r$//' "$OUT"/*                         # normalise line endings
cp "$OUT/utils.c" "$OUT/utils.c.orig"; cp "$OUT/nop.c" "$OUT/nop.c.orig"
# edit 1: <iostream> is C++-only and unused -> the two C headers utils.c needs
sed -i 's|^#include <iostream>|#include <stdint.h>\n#include <sys/types.h>|' "$OUT/utils.c"
# edit 2: print_usage() needs fprintf/stderr (C library); only used by stream_mpi.c's getopt
sed -i '/^void print_usage(char \*argv\[\], char\* usage)$/,/^}$/d' "$OUT/utils.c"
# edit 3: 'extern "C"' is C++ syntax; a C function already has C linkage
sed -i 's/^extern "C" //' "$OUT/nop.c"
# loop.h exactly as their Makefile builds it
awk "{for(i=0; i<${LOOP}; i++)print}" "$OUT/loop_template.h" > "$OUT/loop.h"
# checks
if grep -q 'iostream\|print_usage' "$OUT/utils.c"; then echo "ERROR: utils.c edit failed"; exit 1; fi
if grep -q 'extern "C"' "$OUT/nop.c"; then echo "ERROR: nop.c edit failed"; exit 1; fi
{ diff -u "$OUT/utils.c.orig" "$OUT/utils.c"; diff -u "$OUT/nop.c.orig" "$OUT/nop.c"; } > "$OUT/mess_changes.diff" || true
echo "MESS imported: $(grep -c '^void STREAM_copy_' "$OUT/utils.c") traffic kernels, loop.h = $(wc -l < "$OUT/loop.h") lines"
