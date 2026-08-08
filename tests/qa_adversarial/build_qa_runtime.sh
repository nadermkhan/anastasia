#!/bin/bash
# Builds the runtime-layer adversarial harness against the engine sources.
set -u
ROOT=${ROOT:-/data/work/anastasia}
OUT=${OUT:-/data/rt}
OPT=${OPT:--O2}
mkdir -p "$OUT/obj"
rm -f "$OUT/warn.log"
CXXFLAGS="-std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -ffreestanding -I$ROOT/src $OPT"
LDFLAGS="-nostdlib -nodefaultlibs -Wl,-e,_start"

SRCS=$(find "$ROOT/src" -name '*.cpp' ! -name 'main.cpp')
OBJS=""
for s in $SRCS; do
  o="$OUT/obj/$(echo "${s#$ROOT/}" | tr '/' '_').o"
  g++ $CXXFLAGS -c "$s" -o "$o" 2>>"$OUT/warn.log" || { echo "FAILED: $s"; tail -30 "$OUT/warn.log"; exit 1; }
  OBJS="$OBJS $o"
done
g++ $CXXFLAGS -c "$ROOT/tests/qa_adversarial/qa_runtime.cpp" -o "$OUT/obj/qa_runtime.o" 2>>"$OUT/warn.log" || { echo "FAILED: qa_runtime"; tail -60 "$OUT/warn.log"; exit 1; }
OBJS="$OBJS $OUT/obj/qa_runtime.o"
g++ $CXXFLAGS $LDFLAGS $OBJS -o "$OUT/qa_runtime" || { echo LINK_FAILED; exit 1; }
echo "QA RUNTIME BUILD OK -> $OUT/qa_runtime"
echo "warnings: $(grep -c 'warning:' "$OUT/warn.log" 2>/dev/null || echo 0)"
