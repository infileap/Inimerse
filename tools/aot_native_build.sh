#!/usr/bin/env bash
# Build the route-A translator, `aot-native`, WITHOUT touching CMakeLists.txt.
#
# The aot-backend stream brief freezes CMakeLists.txt for wave 1, so this
# script links src/compilation/aot_native{,_tool}.c against the engine objects
# CMake already produced under build/CMakeFiles/inimerse.dir, excluding
# src/main.c.o (it defines the engine's own main()).
#
# This is a stop-gap for measurement, not the intended final wiring: once the
# coordinator opens CMakeLists.txt, the same two sources become a normal
# `add_executable(aot-native ...)` target and this script is deleted.
#
# Usage: tools/aot_native_build.sh [build-dir] [output]
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-$repo/build}
out=${2:-$build/aot-native}

if [ ! -d "$build/CMakeFiles/inimerse.dir" ]; then
    echo "error: no engine objects under $build — configure and build first:" >&2
    echo "  cmake -S $repo -B $build -DCMAKE_BUILD_TYPE=Release && cmake --build $build -j4" >&2
    exit 1
fi

# Every engine object except the one that owns main().
objs=$(find "$build/CMakeFiles/inimerse.dir" -name '*.o' \
       ! -name 'main.c.o' | sort)
if [ -z "$objs" ]; then
    echo "error: $build/CMakeFiles/inimerse.dir holds no objects" >&2
    exit 1
fi

cc -O2 -std=gnu11 -Wall \
   -I"$repo/src" -I"$repo/src/parser" -I"$repo/src/compiler" -I"$repo/src/vm" \
   -I"$repo/src/runtime" -I"$repo/src/mod" -I"$repo/src/common" \
   -I"$repo/src/lexer" -I"$repo/src/platform" -I"$repo/src/verse" \
   -D_GNU_SOURCE -D_stricmp=strcasecmp \
   -DINIMERSE_VERSION_STRING='"0.5.0"' \
   "$repo/src/compilation/aot_native.c" \
   "$repo/src/compilation/aot_native_tool.c" \
   $objs \
   -o "$out" -lm -lpthread -ldl

echo "built $out"
