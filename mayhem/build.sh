#!/usr/bin/env bash
#
# mayhem/build.sh — build CopyQ's fuzz harnesses (sanitized) and the selftest oracle (normal flags).
#
# CopyQ's own build is CMake over the whole GUI app. The harnesses only need the data-handling units
# (item serialization, command import, the models they fill), so those project sources are compiled
# directly, once per flavour, into a static archive that each harness links:
#   mayhem/build-fuzz/  ASan+UBSan+DWARF-3 objects  -> mayhem/out/<harness>[-standalone]
#   mayhem/build-test/  project's normal flags       -> mayhem/out/copyq_selftest (run by test.sh)
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

SRC=${SRC:-/mayhem}
cd "$SRC"
M="$SRC/mayhem"
OUT="$M/out"
mkdir -p "$OUT"

# Project units the harnesses and the selftest link.
UNITS=(
  src/common/command.cpp
  src/common/commandstore.cpp
  src/common/config.cpp
  src/common/encryption.cpp
  src/common/log.cpp
  src/common/mimetypes.cpp
  src/common/settings.cpp
  src/common/temporaryfile.cpp
  src/common/temporarysettings.cpp
  src/common/textdata.cpp
  src/item/clipboarditem.cpp
  src/item/clipboardmodel.cpp
  src/item/serialize.cpp
)
# Release builds define QT_NO_DEBUG (Q_ASSERT off); QCA is on in the shipped app (WITH_QCA_ENCRYPTION).
DEFS="-DQT_NO_DEBUG -DQT_DEPRECATED_WARNINGS_SINCE=0x060200 -DWITH_QCA_ENCRYPTION"
# libqca-qt6-dev ships no .pc file: add its header dir and library by hand.
QT_MODS="Qt6Core Qt6Gui Qt6Widgets"
QT_CFLAGS="$(pkg-config --cflags $QT_MODS) -I/usr/include/Qca-qt6/QtCrypto -I/usr/include/Qca-qt6"
QT_LIBS="$(pkg-config --libs $QT_MODS) -lqca-qt6"
INCS="-I$SRC/src -I$M"

# compile_unit <dir> <unit> <flags...>
compile_unit() {
  local dir="$1" unit="$2"; shift 2
  "$CXX" -std=c++17 -fPIC $DEFS $INCS $QT_CFLAGS "$@" -c "$SRC/$unit" -o "$dir/$(basename "$unit" .cpp).o"
}
export -f compile_unit
export DEFS INCS QT_CFLAGS SRC

# build_units <dir> <flags...>: compile $UNITS into <dir>/libcopyq-units.a (objects always rebuilt).
build_units() {
  local dir="$1"; shift
  rm -rf "$dir"; mkdir -p "$dir"
  printf '%s\n' "${UNITS[@]}" | xargs -P "$MAYHEM_JOBS" -I{} bash -c 'compile_unit "$@"' _ "$dir" {} "$@"
  ar rcs "$dir/libcopyq-units.a" "$dir"/*.o
}

HARNESSES=(fuzz_deserialize_data fuzz_deserialize_items fuzz_import_commands)

# 1) Sanitized project units + the fuzz and standalone binaries.
FUZZ_FLAGS="-O1 $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link"
build_units "$M/build-fuzz" $FUZZ_FLAGS

$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$M/build-fuzz/standalone_main.o"
$CXX -std=c++17 -fPIC $FUZZ_FLAGS -c "$M/lsan_off.cc" -o "$M/build-fuzz/lsan_off.o"

for h in "${HARNESSES[@]}"; do
  $CXX -std=c++17 -fPIC $DEFS $INCS $QT_CFLAGS $FUZZ_FLAGS -c "$M/$h.cpp" -o "$M/build-fuzz/$h.o"
  $CXX $FUZZ_FLAGS $LIB_FUZZING_ENGINE "$M/build-fuzz/$h.o" "$M/build-fuzz/lsan_off.o" \
       "$M/build-fuzz/libcopyq-units.a" $QT_LIBS -o "$OUT/$h"
  $CXX $FUZZ_FLAGS "$M/build-fuzz/$h.o" "$M/build-fuzz/standalone_main.o" "$M/build-fuzz/lsan_off.o" \
       "$M/build-fuzz/libcopyq-units.a" $QT_LIBS -o "$OUT/$h-standalone"
done

# 2) Selftest oracle: the same project units with NORMAL flags (no sanitizers), run by test.sh.
TEST_FLAGS="-O2 $COVERAGE_FLAGS"
build_units "$M/build-test" $TEST_FLAGS
$CXX -std=c++17 -fPIC $DEFS $INCS $QT_CFLAGS $TEST_FLAGS -c "$M/selftest.cpp" -o "$M/build-test/selftest.o"
$CXX $TEST_FLAGS "$M/build-test/selftest.o" "$M/build-test/libcopyq-units.a" $QT_LIBS -o "$OUT/copyq_selftest"
