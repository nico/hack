# fastpng: the world's fastest PNG decoder

(Probably. On an Apple M4 Max, against the decoders below.)

`fastpng.c` is a complete PNG decoder in one C file, about 2,000 lines with comments, with a
four-function API in `fastpng.h`. It decodes every PNG type (all color types and bit depths,
palettes, tRNS transparency, Adam7 interlacing) to 8-bit RGBA or BGRA. It's a port of the decoder
in SerenityOS's LibGfx and LibCompress, which grew these techniques while chasing PDF rendering
speed.

```
make && ./demo image.png -n 10 -o out.pam [--check-crc] [--check-adler32] [--max-pixels N]
```

```c
#include "fastpng.h"
uint32_t w, h;
int error;
uint8_t* rgba = fastpng_decode_alloc(png_bytes, png_size, FASTPNG_RGBA, NULL, &w, &h, &error);
// Or verify every chunk's CRC and the zlib Adler-32 (about 8% slower), and allow up to 1 gigapixel
// (the default limit is 2^28 pixels, about 268 megapixels):
fastpng_options const options = { FASTPNG_CHECK_ALL, 1000000000 };
uint8_t* checked = fastpng_decode_alloc(png_bytes, png_size, FASTPNG_RGBA, &options, &w, &h, &error);
```

## Speed

![Total decode time over 41 PNGs: fastpng 100.3 ms, fastpng checking checksums 109.4, png crate (Rust) 181.9, libpng 231.8, wuffs 236.3, stb_image 296.2, lodepng 419.2; on fpng-written copies fastpng 122.2 ms, fastpng checking checksums 136.6, fpng 167.6, libpng 243.3, wuffs 320.0, png crate 340.5, stb_image 411.7, lodepng 526.1](speed.svg)

Total ms over 41 public PNGs chosen to be representative of the web and of documents (screenshots,
photos, maps, charts, sprites, icons, scans, PDF page renders; every color type, bit depths 1 to 16,
interlaced and not; see `bench/CORPUS.md`), best of 9 runs each, into freshly allocated output.
Apple M4 Max, clang -O2 for fastpng, -O3 for the C and C++ decoders, release + LTO for the png
crate. fpng only decodes files fpng wrote, so the second column is the same images re-encoded by
fpng:

| decoder | checksums checked | 41 PNGs, ms | fastpng's time / theirs | fpng-written copies, ms | fastpng's time / theirs |
|---|---|---|---|---|---|
| **fastpng** | none | **100.3** | 1.00 | **122.2** | 1.00 |
| fastpng | CRC + Adler-32 | 109.4 | 0.92 | 136.6 | 0.89 |
| png crate (Rust; what Chromium uses) | CRC | 181.9 | 0.55 | 340.5 | 0.36 |
| png crate | none | 179.3 | 0.56 | 336.8 | 0.36 |
| wuffs | CRC + Adler-32 | 236.3 | 0.42 | 320.0 | 0.38 |
| wuffs | none | 229.2 | 0.44 | 308.2 | 0.40 |
| libpng | CRC + Adler-32 | 231.8 | 0.43 | 243.3 | 0.50 |
| stb_image | none | 296.2 | 0.34 | 411.7 | 0.30 |
| lodepng | CRC + Adler-32 | 419.2 | 0.24 | 526.1 | 0.23 |
| fpng | small chunks' CRCs | can't decode | | 167.6 | 0.73 |

With checksums checked, pixels and errors match wuffs' on PngSuite and the 41 images (217 files),
and on wuffs' test data and SerenityOS's PNG tests. Unchecked, the only difference is PngSuite's two
files with deliberately broken checksums, which fastpng then decodes.

What's checked affects speed, so the table says it: by default fastpng checks no CRCs and not
the zlib Adler-32 (`FASTPNG_CHECK_CRC` and `FASTPNG_CHECK_ADLER32` turn them on), and the png
crate by default checks chunk CRCs but not the Adler-32. Compare rows that check the same.

Where the png crate's time goes, on the 1600x1600 Paeth-filtered mandrill (21.1 ms vs fastpng's
9.6): inflate (fdeflate) about 7.6 ms, the same as fastpng's 7.5; unfiltering one row at a time
about 8.9 ms against fastpng's 1.4 eight rows at a time; and copying between its streaming buffers
about 3.3 ms.

## The key ideas

The time goes to two places: inflating the zlib stream, and undoing the row filters. Everything
else (unpacking to RGBA) should ride along while the data is in cache.

**Inflate: libdeflate's design** (Eric Biggers):

- *Fat table entries.* A decode table entry carries everything about a symbol: whether it's a
  literal, a length or end-of-block, its value or base, and the number of bits to drop for the code
  *and* its extra bits together. A literal is one load, one shift, one store; one shift consumes a
  length code and its extra bits, and the extra bits are what was shifted out.
- *Branchless refill.* Load 8 bytes unaligned, OR them in above the bits held, advance the input by
  however many whole bytes fit. No loop, no branch. A refill guarantees 56 bits, which is budgeted
  so that up to three literals, or a literal and a whole match, decode between refills.
- *A fast loop with no bounds checks.* While the input and output are more than a margin from their
  ends, nothing is checked; a careful loop decodes the last few hundred bytes.
- *Overlapping word copies.* Matches copy 8 bytes at a time, writing past the match's end into the
  margin. Distance 1 broadcasts a byte.
- *Latency hiding.* The next table entry is loaded before the current match is copied.
- *Hot state in locals.* The fast loop works on local copies of the bit buffer and pointers, which
  stay in registers. Kept in a struct in memory, the same loop runs 2-3x slower.

**Inflate: beyond libdeflate.**

- *Two literals per table entry* (fpng's trick). When a literal's code is short enough that the
  next code also fits in the table's 11 bits, the entry holds both bytes. One lookup, one shift,
  one 2-byte store produce two bytes. Decoding literals is a serial lookup→shift→lookup chain, and
  this shortens it where literal codes are short. That's mostly fpng's own output (−23% decode
  cycles on fpng-written files). It also helps some smooth real-world images: 3 of the 19 bench
  PNGs got 16-18% faster, while the rest never pair, and PDF images (mostly matches) don't gain.
  Pairing a table costs as much as building it, so it's done only when the code lengths say a
  quarter or more of the entries pair, and paired blocks run their own copy of the fast loop.
  Single-literal blocks keep the 1-byte store and pay nothing.
- *Short-distance matches as vector repeats.* A match 2-7 bytes back reads bytes it's writing. Word
  copies then advance only 2-7 bytes each, every load waiting for the store before it: 3 bytes per
  step for a run of RGB pixels. For matches of 16+ bytes, the pattern is built once with a constant
  shuffle per distance, and 12-16 bytes are stored at a time with nothing to wait for.
- *No copy of a single IDAT chunk.* With one chunk, inflate reads it in place; several are joined
  first, which costs 1-2%.

**Unfiltering.** None and Up are trivially vectorized. Sub, Average and Paeth each predict a byte
from its left neighbour, so every row is a dependency chain across its pixels. The tricks shorten
the chain or run several at once:

- *Sub as a prefix sum.* A 16-byte vector adds itself shifted by 1, 2, 4… pixels (constant
  shuffles), then the previous vector's last pixel. That's one dependent add per 16 bytes, not per
  pixel. Each next vector is loaded *before* the current one is stored: with 3-byte pixels a vector
  holds 15 bytes, and a load overlapping the previous store would stall on store-to-load forwarding.
  Getting that wrong made it 30-65% slower than scalar.
- *Left pixel in registers.* Scalar Average and Paeth keep the left pixel in registers, not re-read
  from memory, with the per-channel loop unrolled for each pixel size.
- *A cheaper Paeth.* The scalar predictor uses stb_image's formulation (`3c - (a+b)` against min/max
  of a and b). It's equivalent to the spec on all 2^24 inputs, and puts fewer operations on the
  chain through `a`. The vector one is FPNGE's: everything in 8 bits with absolute differences and
  saturation, no widening.
- *Diagonal multi-row unfiltering* (from wuffs issue 157). A pixel needs the row above only up to
  one pixel to its right, so row r can work on pixel x−r while row 0 works on pixel x. Four rows of
  RGB or RGBA pixels fill one 16-byte vector, one pixel per row per lane group. Average and Paeth
  run four rows per step. Paeth, whose chain is long, runs eight rows in two vectors in about the
  time of four. Measured: about 2x for Average, 1.7-1.9x for 4-row Paeth, 3.1x for 8-row Paeth.
  Small pixels (1-3 bytes) run Average two rows at once.
- *No vector lanes through memory.* Kernel state lives in named vector variables, and lanes move
  between vectors by shuffles. Arrays of vectors and lane extraction compiled to stack round trips
  with store-forwarding stalls every step. Check the disassembly for `[sp` in hot loops.

**Unpacking.**

- *While it's in cache.* Rows are unfiltered in place in the inflated buffer 16 at a time, and each
  batch goes straight to the output. There's no second pass and no intermediate image.
- *One shuffle per 16 bytes.* RGB→RGBA puts four pixels in place, with alpha from an all-0xff
  vector. RGBA→BGRA is a byte swap shuffle, and RGBA→RGBA a memcpy. Gray and gray+alpha expand by
  shuffles too.
- *Tables of finished pixels.* Palettes and 1/2/4-bit grays go through 256-entry tables of output
  pixels. The tables already include tRNS alpha and the low-bit-depth scaling (1, 3, 15 → 255). The
  palette index check is per row, not per pixel.

**Checksums, when asked for.** Unasked, none of this code runs.

- *CRC-32 on the CPU's CRC32 instructions* (8 bytes each, about 12 GB/s here) where the target has
  them, through `__builtin_arm_crc32d` behind `__ARM_FEATURE_CRC32`; elsewhere slice-by-8 tables,
  built per decode instead of kept in a global.
- *Adler-32 in vectors, at about 28 GB/s.* Each 16 bytes add pairwise into four lanes of byte sums
  and into sixteen 16-bit column sums. The 16..1 weights apply to the column sums once per 4 KB
  block, not per byte: the weighted sum of every 16 bytes is the weighted sum of the columns.
- *Adler-32 while it's in cache.* It runs over each 16-row batch of inflated data just before that
  batch is unfiltered in place, which also has to come first since unfiltering changes the bytes.
- *Out of line on purpose.* Inlined into the row loop, the Adler-32 code slowed the unfiltering
  around it by half: checking cost 2x the decode time on some images instead of 1.2x.

**Portable SIMD without intrinsics.** All vector code uses GCC/clang vector extensions
(`vector_size`, element-wise operators, comparison masks, casts) and `__builtin_shufflevector` with
constant indices. Clang turns that into tbl/ext/uabd/uhadd/uqadd on NEON and pshufb/palignr on SSE.
Section 1 of `fastpng.c` has the handful of helpers; nothing is architecture-specific, except
the optional use of CRC32 instructions, with a portable fallback.

## Limits and trade-offs

- Ignores gamma, chromaticities and ICC profiles (like wuffs and stb_image), and APNG animation (it
  decodes the default image). 16-bit samples keep their high byte.
- Holds the whole inflated image in memory: one buffer of height × (row bytes + 1), plus the
  output. A streaming decoder would use far less memory, at some speed. Images over
  `max_pixels` (by default 2^28, about 268 megapixels or 1 GiB of output) are rejected from the
  header, before anything big is allocated, so decoding needs at most about 3 GiB by default
  (plus a copy of the compressed data when it's split over several chunks). Image data also can't
  claim more than deflate's 1032:1 expansion of its size.
- Single-threaded by design.
- Needs clang, or gcc 12+ (gcc's build is ~15% slower), and a little-endian CPU. Tested on arm64
  (correctness and fuzzing, clang and gcc) and on wasm32 with SIMD under wasmtime (byte-identical
  output). It also builds for x86-64, but hasn't been run there.

## Files

- `fastpng.h`, `fastpng.c`: the decoder. `fastpng.c` sections:
  1. portable SIMD
  2. inflate
  3. checksums
  4. unfiltering
  5. unpacking
  6. the PNG container and API
- `demo.c`: decodes a file, prints the time, optionally writes a PAM and checks checksums.
- `bench/`: the comparison, independent of the rest.
  - `run.sh` reproduces the table: it builds everything, downloads the images listed in
    `images.txt` into `bench/build/images/` once (checking their SHA-256s), and runs the benchmark
    on them as written and re-encoded by fpng. With `PNGSUITE=dir` it first runs `--check` on
    PngSuite. Extra arguments go to png-bench, e.g. `bench/run.sh --only fastpng,png-rs`.
  - `CORPUS.md` says what's in the image set and why.
  - `build.sh` builds `bench/build/png-bench` against checkouts of wuffs, stb, fpng (which also
    provides lodepng) and libpng; paths are set with `WUFFS=`, `STB=`, `FPNG=`, `LIBPNG=`. With
    cargo around, it also builds `rust-png/`, a C-callable wrapper around the png crate (the
    columns `png-rs`, with the crate's default checks, and `png-rs-nc`, without).
  - `build/png-bench -n 9 *.png` prints a table with a column per decoder.
  - `--fpng-reencode` benchmarks fpng-encoded copies.
  - `--check` compares fastpng's pixels and errors against wuffs' file by file, with checksums
    checked, and checks that unchecked decoding gives the same pixels.
  - `--only fastpng,wuffs` limits the columns.
  - `make_chart.py` regenerates `speed.svg` from the totals written into it:
    `uv run bench/make_chart.py`.
  - `fuzz.c` is a mutation fuzzer (also a libFuzzer target) that runs under ASan/UBSan. It checks
    that corrupt files fail cleanly in every checksum mode, that BGRA is RGBA swapped, and that a
    file that decodes with checksums decodes the same without. Its first run caught an
    allocation sized from a corrupt header, now rejected using the 1032:1 deflate bound.

## Credits

These ideas come from:

- libdeflate (Eric Biggers): the inflate design.
- wuffs (Nigel Tao): its "fastest, safest PNG decoder" write-up and issue 157.
- fpng (Rich Geldreich): literal pairs.
- FPNGE (Luca Versari, Zoltan Szabadka): 8-bit vector Paeth.
- stb_image (Sean Barrett): the scalar Paeth.
- The optidash "missing SIMD code" post.

The code was written for SerenityOS and ported here.
