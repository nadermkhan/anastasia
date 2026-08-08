#!/bin/bash
# Builds baseline_probe.cpp against an arbitrary engine source tree so the
# identical probe can be run against the pre-fix and post-fix sources.
set -u
SRC=${SRC:?set SRC to a source root}
OUT=${OUT:?set OUT}
OPT=${OPT:--O2}
mkdir -p "$OUT/obj"
rm -f "$OUT/warn.log"
CXXFLAGS="-std=c++20 -fno-exceptions -fno-rtti -ffreestanding -I$SRC $OPT"
LDFLAGS="-nostdlib -nodefaultlibs -Wl,-e,_start"

SRCS=$(find "$SRC" -name '*.cpp' ! -name 'main.cpp')
OBJS=""
for s in $SRCS; do
  o="$OUT/obj/$(echo "${s#$SRC/}" | tr '/' '_').o"
  g++ $CXXFLAGS -c "$s" -o "$o" 2>>"$OUT/warn.log" || { echo "COMPILE_FAILED: $s"; tail -20 "$OUT/warn.log"; exit 1; }
  OBJS="$OBJS $o"
done
g++ $CXXFLAGS -c /data/fix/baseline_probe.cpp -o "$OUT/obj/probe.o" 2>>"$OUT/warn.log" || { echo "COMPILE_FAILED: probe"; tail -40 "$OUT/warn.log"; exit 1; }
g++ $CXXFLAGS $LDFLAGS $OBJS "$OUT/obj/probe.o" -o "$OUT/probe" || { echo LINK_FAILED; exit 1; }
echo "PROBE BUILD OK -> $OUT/probe"
