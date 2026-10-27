#!/bin/bash
# Builds bench/build/png-bench. The other decoders come from checkouts; override where they are:
#   WUFFS=~/src/wuffs  STB=~/src/stb  FPNG=~/src/fpng  LIBPNG=$(brew --prefix libpng)  BLEND2D=~/src/blend2d
# The third-party decoders build at -O3 (as wuffs recommends); fastpng at $FASTPNG_FLAGS. With
# cargo on the PATH (or CARGO=...), the Rust png crate is included too (rust-png/, release + LTO);
# with a Blend2D checkout and cmake, Blend2D too (its own release build: -O3/-O2, static, no JIT).
set -e
cd "$(dirname "$0")"
WUFFS=${WUFFS:-~/src/wuffs}
STB=${STB:-~/src/stb}
FPNG=${FPNG:-~/src/fpng}
LIBPNG=${LIBPNG:-$(brew --prefix libpng 2>/dev/null || echo ~/src/homebrew/opt/libpng)}
FASTPNG_FLAGS=${FASTPNG_FLAGS:--O2}
CC=${CC:-clang}
CXX=${CXX:-clang++}
mkdir -p build
B=build

$CC -O3 -c -DWUFFS_IMPLEMENTATION -DWUFFS_CONFIG__MODULES -DWUFFS_CONFIG__MODULE__BASE \
    -DWUFFS_CONFIG__MODULE__ADLER32 -DWUFFS_CONFIG__MODULE__CRC32 -DWUFFS_CONFIG__MODULE__DEFLATE \
    -DWUFFS_CONFIG__MODULE__ZLIB -DWUFFS_CONFIG__MODULE__PNG $WUFFS/release/c/wuffs-v0.4.c -o $B/wuffs.o &
printf '#define STB_IMAGE_IMPLEMENTATION\n#define STBI_ONLY_PNG\n#include "stb_image.h"\n' > $B/stb_impl.c
$CC -O3 -DNDEBUG -c -I$STB $B/stb_impl.c -o $B/stb.o &
$CXX -O3 -DNDEBUG -std=c++17 -fno-strict-aliasing -w -c $FPNG/src/fpng.cpp -o $B/fpng.o &
$CXX -O3 -DNDEBUG -std=c++17 -w -c $FPNG/src/lodepng.cpp -o $B/lodepng.o &
$CC $FASTPNG_FLAGS -std=c11 -Wall -Wextra -c ../fastpng.c -o $B/fastpng.o &
CARGO=${CARGO:-$(command -v cargo || echo ~/.cargo/bin/cargo)}
RUST_FLAGS=()
if [ -x "$CARGO" ]; then
    (cd rust-png && "$CARGO" build --release --quiet --target-dir ../$B/rust-target)
    RUST_FLAGS=(-DHAVE_RUST_PNG $B/rust-target/release/librustpng_bench.a)
fi
BLEND2D=${BLEND2D:-~/src/blend2d}
CMAKE=${CMAKE:-$(command -v cmake || echo /Applications/CMake.app/Contents/bin/cmake)}
BLEND2D_FLAGS=()
if [ -f "$BLEND2D/CMakeLists.txt" ] && [ -x "$CMAKE" ]; then
    "$CMAKE" -S "$BLEND2D" -B $B/blend2d -DCMAKE_BUILD_TYPE=Release -DBLEND2D_STATIC=ON -DBLEND2D_NO_JIT=ON \
        -DBLEND2D_NO_INSTALL=ON > /dev/null
    "$CMAKE" --build $B/blend2d -j 8 > /dev/null
    BLEND2D_FLAGS=(-DHAVE_BLEND2D -DBL_STATIC -I"$BLEND2D" $B/blend2d/libblend2d.a)
fi
wait
$CXX -O2 -std=c++17 -I.. -I$STB -I$FPNG/src -I$WUFFS/release/c -I$LIBPNG/include bench.cpp "${RUST_FLAGS[@]}" "${BLEND2D_FLAGS[@]}" \
    $B/fastpng.o $B/wuffs.o $B/stb.o $B/fpng.o $B/lodepng.o -L$LIBPNG/lib -lpng -lz -Wl,-rpath,$LIBPNG/lib -o $B/png-bench
echo "built $(pwd)/$B/png-bench"
