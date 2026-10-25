// A small mutation fuzzer for fastpng: truncates and corrupts the given PNGs and decodes the
// results, which must fail cleanly or succeed, never crash. Also checks that BGRA output is RGBA
// output with red and blue swapped. Build with sanitizers:
//   clang -O1 -g -fsanitize=address,undefined -I.. fuzz.c ../fastpng.c -o build/fuzz
//   build/fuzz [-n iterations-per-file] file.png...
// It also works as a libFuzzer target: clang -fsanitize=fuzzer,address -DLIBFUZZER ...

#include "fastpng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void decode_both_ways_with(uint8_t const* data, size_t size, unsigned flags)
{
    uint32_t w, h;
    int error;
    fastpng_options const options = { flags, 0 };
    uint8_t* rgba = fastpng_decode_alloc(data, size, FASTPNG_RGBA, &options, &w, &h, &error);
    uint8_t* bgra = fastpng_decode_alloc(data, size, FASTPNG_BGRA, &options, &w, &h, &error);
    if (rgba && bgra) {
        for (size_t i = 0; i < (size_t)w * h * 4; i += 4) {
            if (rgba[i] != bgra[i + 2] || rgba[i + 1] != bgra[i + 1] || rgba[i + 2] != bgra[i] || rgba[i + 3] != bgra[i + 3]) {
                fprintf(stderr, "BGRA output isn't RGBA swapped at byte %zu\n", i);
                abort();
            }
        }
    } else if (rgba || bgra) {
        fprintf(stderr, "one format decoded and the other didn't\n");
        abort();
    }
    free(rgba);
    free(bgra);
}

static uint64_t piece_rng = 0x9e3779b97f4a7c15ull;

// Decoding in random pieces gives the same result as in one step: the same pixels, or an error
// (which may name a different problem in a file with several, met in another order).
static void decode_in_pieces(uint8_t const* data, size_t size)
{
    uint32_t w, h;
    if (fastpng_get_size(data, size, NULL, &w, &h) != FASTPNG_OK)
        return;
    size_t const bytes = (size_t)w * h * 4;
    uint8_t* once = malloc(bytes);
    uint8_t* pieces = malloc(bytes);
    if (!once || !pieces) {
        free(once);
        free(pieces);
        return;
    }
    int const expected = fastpng_decode(data, size, FASTPNG_RGBA, once, (size_t)w * 4, NULL);
    // Every other time with interlace blocks filled, which changes only what's shown on the way.
    fastpng_options const fill = { FASTPNG_FILL_INTERLACE_PASSES, 0 };
    fastpng_decoder* d = fastpng_decoder_create(piece_rng & 1 ? &fill : NULL);
    int status = FASTPNG_NEED_MORE_DATA;
    size_t have = 0;
    while (status == FASTPNG_NEED_MORE_DATA && have < size) {
        piece_rng ^= piece_rng << 13;
        piece_rng ^= piece_rng >> 7;
        piece_rng ^= piece_rng << 17;
        have += 1 + piece_rng % (size / 8 + 1);
        if (have > size)
            have = size;
        status = fastpng_decoder_update(d, data, have, have == size, FASTPNG_RGBA, pieces, (size_t)w * 4, NULL);
    }
    fastpng_decoder_destroy(d);
    if ((status == FASTPNG_OK) != (expected == FASTPNG_OK) || (status == FASTPNG_OK && memcmp(once, pieces, bytes) != 0)) {
        fprintf(stderr, "decoding in pieces: %s, in one step: %s (input in build/fuzz-failure.png)\n", fastpng_error_string(status), fastpng_error_string(expected));
        FILE* f = fopen("build/fuzz-failure.png", "wb");
        if (f) {
            fwrite(data, 1, size, f);
            fclose(f);
        }
        abort();
    }
    free(once);
    free(pieces);
}

// Without checksums and with each of them; a file that decodes when checked decodes unchecked.
static void decode_both_ways(uint8_t const* data, size_t size)
{
    uint32_t w, h;
    int error;
    decode_in_pieces(data, size);
    decode_both_ways_with(data, size, 0);
    decode_both_ways_with(data, size, FASTPNG_CHECK_CRC);
    decode_both_ways_with(data, size, FASTPNG_CHECK_ADLER32);
    fastpng_options const check_all = { FASTPNG_CHECK_ALL, 0 };
    uint8_t* checked = fastpng_decode_alloc(data, size, FASTPNG_RGBA, &check_all, &w, &h, &error);
    if (checked) {
        uint8_t* unchecked = fastpng_decode_alloc(data, size, FASTPNG_RGBA, NULL, &w, &h, &error);
        if (!unchecked || memcmp(checked, unchecked, (size_t)w * h * 4) != 0) {
            fprintf(stderr, "decoding with checksums and without differs\n");
            abort();
        }
        free(unchecked);
    }
    free(checked);
}

#ifdef LIBFUZZER
int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    decode_both_ways(data, size);
    return 0;
}
#else
static uint64_t rng_state = 0x9e3779b97f4a7c15ull;
static uint64_t next_random(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

int main(int argc, char** argv)
{
    int iterations = 200;
    int first = 1;
    if (argc > 2 && !strcmp(argv[1], "-n")) {
        iterations = atoi(argv[2]);
        first = 3;
    }
    for (int i = first; i < argc; ++i) {
        FILE* f = fopen(argv[i], "rb");
        if (!f)
            continue;
        fseek(f, 0, SEEK_END);
        size_t const size = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t* original = malloc(size);
        if (fread(original, 1, size, f) != size) {
            fclose(f);
            free(original);
            continue;
        }
        fclose(f);
        decode_both_ways(original, size);
        for (int n = 0; n < iterations; ++n) {
            // An exactly sized copy, so reading past the end is caught.
            size_t mutated_size = size;
            if (n % 4 == 0)
                mutated_size = next_random() % (size + 1);
            uint8_t* mutated = malloc(mutated_size ? mutated_size : 1);
            memcpy(mutated, original, mutated_size);
            if (n % 4 != 0 && mutated_size > 0) {
                int const flips = 1 + next_random() % 8;
                for (int k = 0; k < flips; ++k)
                    mutated[next_random() % mutated_size] ^= (uint8_t)(1 + next_random() % 255);
            }
            decode_both_ways(mutated, mutated_size);
            free(mutated);
        }
        free(original);
    }
    printf("fuzzed %d files, %d mutations each: no crashes\n", argc - first, iterations);
    return 0;
}
#endif
