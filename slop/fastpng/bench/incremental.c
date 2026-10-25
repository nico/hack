// Tests and times fastpng's incremental decoding. For each file: decodes it in one step; then
// feeds it to a decoder in random pieces, checking after each that the rows reported final already
// have their final pixels, and at the end that the pixels and the result match the one-step
// decode. Then times one step, incremental decoding in `pieces` pieces, and decoding from scratch
// at every piece (what a decoder that can't resume does):
//   clang -O2 -I.. incremental.c ../fastpng.c -o build/incremental
//   build/incremental [-p pieces] [-t trials] file.png...

#include "fastpng.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static uint64_t rng = 0x2545f4914f6cdd1dull;
static uint64_t next_random(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

static uint8_t const adam7_x0[7] = { 0, 4, 0, 2, 0, 1, 0 }, adam7_y0[7] = { 0, 0, 4, 0, 2, 0, 1 };
static uint8_t const adam7_dx[7] = { 8, 8, 4, 4, 2, 2, 1 }, adam7_dy[7] = { 8, 8, 8, 4, 4, 2, 2 };
static uint8_t const fill_w[7] = { 8, 4, 4, 2, 2, 1, 1 }, fill_h[7] = { 8, 8, 4, 4, 2, 2, 1 };

// What an interlaced image should look like with its first `passes` passes decoded and their
// pixels spread over their blocks, worked out from the finished image.
static void interlace_preview(unsigned char const* final, uint32_t w, uint32_t h, int passes, unsigned char* out)
{
    for (int p = 0; p < passes; ++p) {
        for (uint32_t y = adam7_y0[p]; y < h; y += adam7_dy[p]) {
            for (uint32_t x = adam7_x0[p]; x < w; x += adam7_dx[p]) {
                for (uint32_t by = y; by < y + fill_h[p] && by < h; ++by) {
                    for (uint32_t bx = x; bx < x + fill_w[p] && bx < w; ++bx)
                        memcpy(out + 4 * ((size_t)by * w + bx), final + 4 * ((size_t)y * w + x), 4);
                }
            }
        }
    }
}

// Decodes `png` in `pieces` pieces (random cut points when `random_cuts`), copying the data into a
// buffer that grows as a browser's would. With `reference`, checks after each piece that the rows
// reported final have their final pixels, and with `previews` (an interlaced image's, for each
// number of passes, decoded with blocks filled) that each row shown is as it should be after the
// passes done or after the one going on. Returns the final status.
static int decode_in_pieces(unsigned char const* png, size_t size, int pieces, int random_cuts, unsigned flags, unsigned char* pixels, size_t stride, unsigned char const* reference, unsigned char* const* previews, uint32_t height, char const* name)
{
    fastpng_options const options = { flags, 0 };
    fastpng_decoder* d = fastpng_decoder_create(&options);
    unsigned char* received = malloc(size ? size : 1);
    size_t have = 0;
    int status = FASTPNG_NEED_MORE_DATA;
    fastpng_progress last = { 0, 0, 0 };
    for (int i = 1; status == FASTPNG_NEED_MORE_DATA && have < size; ++i) {
        size_t next = random_cuts ? have + 1 + next_random() % (2 * size / pieces + 1) : size * i / pieces;
        if (next > size || i == pieces)
            next = size;
        if (next <= have)
            continue;
        memcpy(received + have, png + have, next - have);
        have = next;
        fastpng_progress progress;
        status = fastpng_decoder_update(d, received, have, have == size, FASTPNG_RGBA, pixels, stride, &progress);
        if (progress.rows_final < last.rows_final || progress.rows_shown < last.rows_shown || progress.passes_done < last.passes_done || progress.rows_shown < progress.rows_final) {
            fprintf(stderr, "%s: progress went back or doesn't add up\n", name);
            exit(1);
        }
        last = progress;
        if (reference && progress.rows_final && memcmp(pixels, reference, (size_t)progress.rows_final * stride) != 0) {
            fprintf(stderr, "%s: a row reported final at %zu of %zu bytes differs from the one-step decode\n", name, have, size);
            exit(1);
        }
        if (previews && status == FASTPNG_NEED_MORE_DATA) {
            unsigned const k = progress.passes_done;
            for (uint32_t y = 0; y < progress.rows_shown; ++y) {
                unsigned char const* row = pixels + y * stride;
                bool const as_done = k > 0 && memcmp(row, previews[k] + y * stride, stride) == 0;
                bool const as_next = k < 7 && memcmp(row, previews[k + 1] + y * stride, stride) == 0;
                if (!as_done && !as_next) {
                    fprintf(stderr, "%s: row %u shown with %u passes done isn't a preview of them\n", name, y, k);
                    exit(1);
                }
            }
        }
    }
    if (status == FASTPNG_OK && (last.rows_final != height || last.rows_shown != height)) {
        fprintf(stderr, "%s: done, but %u rows final, %u shown, of %u\n", name, last.rows_final, last.rows_shown, height);
        exit(1);
    }
    fastpng_decoder_destroy(d);
    free(received);
    return status;
}

int main(int argc, char** argv)
{
    int pieces = 64;
    int trials = 20;
    int first = 1;
    for (; first + 1 < argc && argv[first][0] == '-'; first += 2) {
        if (!strcmp(argv[first], "-p"))
            pieces = atoi(argv[first + 1]);
        else if (!strcmp(argv[first], "-t"))
            trials = atoi(argv[first + 1]);
    }
    double total_once = 0, total_pieces = 0, total_restart = 0;
    printf("%-30s %9s %12s %14s   (ms; %d pieces)\n", "file", "one step", "incremental", "from scratch", pieces);
    for (int i = first; i < argc; ++i) {
        FILE* f = fopen(argv[i], "rb");
        if (!f)
            continue;
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);
        unsigned char* png = malloc(size);
        size_t got = fread(png, 1, size, f);
        fclose(f);
        if (got != size)
            continue;
        char const* name = strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i];
        uint32_t w, h;
        if (fastpng_get_size(png, size, NULL, &w, &h) != FASTPNG_OK) {
            free(png);
            continue;
        }
        size_t const stride = (size_t)w * 4;
        unsigned char* reference = malloc(stride * h);
        unsigned char* pixels = malloc(stride * h);
        int const expected = fastpng_decode(png, size, FASTPNG_RGBA, reference, stride, NULL);

        // An interlaced image's previews, for checking the blocks filled in as it arrives.
        bool const interlaced = size > 28 && png[28] == 1;
        unsigned char* previews[8] = { 0 };
        if (interlaced && expected == FASTPNG_OK) {
            for (int k = 1; k <= 7; ++k) {
                previews[k] = malloc(stride * h);
                interlace_preview(reference, w, h, k, previews[k]);
            }
        }

        // Correctness: random pieces, a few times over, with interlace blocks filled every other time.
        for (int t = 0; t < trials; ++t) {
            bool const fill = t % 2;
            int const status = decode_in_pieces(png, size, pieces, 1, fill ? FASTPNG_FILL_INTERLACE_PASSES : 0, pixels, stride, expected == FASTPNG_OK ? reference : NULL, fill && previews[1] ? previews : NULL, h, name);
            // An error may name a different problem in a file with several, met in another order.
            if ((status == FASTPNG_OK) != (expected == FASTPNG_OK) || (status == FASTPNG_OK && memcmp(pixels, reference, stride * h) != 0)) {
                fprintf(stderr, "%s: in pieces: %s, in one step: %s%s\n", name, fastpng_error_string(status), fastpng_error_string(expected), status == expected ? " (pixels differ)" : "");
                return 1;
            }
        }
        for (int k = 1; k <= 7; ++k)
            free(previews[k]);
        if (expected != FASTPNG_OK) {
            printf("%-30.30s fails the same either way (%s)\n", name, fastpng_error_string(expected));
            continue;
        }

        // Time: best of a few runs each.
        double once = 1e30, in_pieces = 1e30, restart = 1e30;
        for (int r = 0; r < 5; ++r) {
            double t = now_ms();
            fastpng_decode(png, size, FASTPNG_RGBA, pixels, stride, NULL);
            once = (t = now_ms() - t) < once ? t : once;
            t = now_ms();
            decode_in_pieces(png, size, pieces, 0, FASTPNG_FILL_INTERLACE_PASSES, pixels, stride, NULL, NULL, h, name);
            in_pieces = (t = now_ms() - t) < in_pieces ? t : in_pieces;
            if (r < 2) {
                // From scratch on each prefix, the way a decoder that can't resume would.
                t = now_ms();
                for (int k = 1; k <= pieces; ++k) {
                    fastpng_decoder* d = fastpng_decoder_create(NULL);
                    fastpng_decoder_update(d, png, size * k / pieces, k == pieces, FASTPNG_RGBA, pixels, stride, NULL);
                    fastpng_decoder_destroy(d);
                }
                restart = (t = now_ms() - t) < restart ? t : restart;
            }
        }
        printf("%-30.30s %9.2f %12.2f %14.2f\n", name, once, in_pieces, restart);
        total_once += once;
        total_pieces += in_pieces;
        total_restart += restart;
        free(png);
        free(reference);
        free(pixels);
    }
    printf("%-30s %9.2f %12.2f %14.2f\n", "TOTAL", total_once, total_pieces, total_restart);
    return 0;
}
