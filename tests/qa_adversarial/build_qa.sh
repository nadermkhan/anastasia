#!/bin/bash
# Builds the independent QA harness against the engine sources.
set -u
ROOT=${ROOT:-/data/work/anastasia}
OUT=${OUT:-/data/qa/out}
OPT=${OPT:--O2}
mkdir -p "$OUT/obj"
CXXFLAGS="-std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -ffreestanding -I$ROOT/src $OPT"
LDFLAGS="-nostdlib -nodefaultlibs -Wl,-e,_start"

SRCS=$(find "$ROOT/src" -name '*.cpp' ! -name 'main.cpp')
OBJS=""
for s in $SRCS; do
  o="$OUT/obj/$(echo "${s#$ROOT/}" | tr '/' '_').o"
  g++ $CXXFLAGS -c "$s" -o "$o" 2>>"$OUT/warn.log" || { echo "FAILED: $s"; tail -30 "$OUT/warn.log"; exit 1; }
  OBJS="$OBJS $o"
done
g++ $CXXFLAGS -c /data/qa/qa_main.cpp -o "$OUT/obj/qa_main.o" 2>>"$OUT/warn.log" || { echo "FAILED: qa_main"; tail -60 "$OUT/warn.log"; exit 1; }
OBJS="$OBJS $OUT/obj/qa_main.o"
g++ $CXXFLAGS $LDFLAGS $OBJS -o "$OUT/qa" || { echo LINK_FAILED; exit 1; }
echo "QA BUILD OK -> $OUT/qa"
