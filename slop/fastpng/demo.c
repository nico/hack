// fastpng demo: decodes a PNG, says how long it took, and optionally writes the pixels as a PAM
// (RGBA) file that most image viewers and ImageMagick read. With --pieces, it feeds the file to the
// incremental decoder in that many pieces, as if it were arriving over the network.
//
//   ./demo image.png [-n repeats] [-o out.pam] [--pieces N] [--check-crc] [--check-adler32]
//          [--max-pixels N]

#define _POSIX_C_SOURCE 199309L // clock_gettime

#include "fastpng.h"

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

static unsigned char* read_file(char const* path, size_t* size)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long const length = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* data = malloc(length > 0 ? length : 1);
    if (data && fread(data, 1, length, f) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = length;
    return data;
}

int main(int argc, char** argv)
{
    char const* input = NULL;
    char const* output = NULL;
    int repeats = 1;
    int pieces = 0;
    fastpng_options options = { 0, 0 };
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc)
            repeats = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc)
            output = argv[++i];
        else if (!strcmp(argv[i], "--pieces") && i + 1 < argc)
            pieces = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--check-crc"))
            options.flags |= FASTPNG_CHECK_CRC;
        else if (!strcmp(argv[i], "--check-adler32"))
            options.flags |= FASTPNG_CHECK_ADLER32;
        else if (!strcmp(argv[i], "--max-pixels") && i + 1 < argc)
            options.max_pixels = strtoull(argv[++i], NULL, 0);
        else
            input = argv[i];
    }
    if (!input || repeats < 1) {
        fprintf(stderr, "usage: %s image.png [-n repeats] [-o out.pam] [--pieces N] [--check-crc] [--check-adler32] [--max-pixels N]\n", argv[0]);
        return 1;
    }

    size_t size;
    unsigned char* png = read_file(input, &size);
    if (!png) {
        fprintf(stderr, "can't read %s\n", input);
        return 1;
    }

    uint32_t width, height;
    int error = fastpng_get_size(png, size, &options, &width, &height);
    if (error) {
        fprintf(stderr, "%s: %s\n", input, fastpng_error_string(error));
        return 1;
    }
    unsigned char* pixels = malloc((size_t)width * height * 4);
    if (!pixels) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    double best = 1e30;
    for (int r = 0; r < repeats; ++r) {
        double const start = now_ms();
        if (pieces > 0) {
            // Each update gets all of the file so far, and says how many rows can be shown. An
            // interlaced image shows coarse early, with each pass's pixels filling their blocks.
            fastpng_options incremental_options = options;
            incremental_options.flags |= FASTPNG_FILL_INTERLACE_PASSES;
            fastpng_decoder* decoder = fastpng_decoder_create(&incremental_options);
            if (!decoder) {
                fprintf(stderr, "out of memory\n");
                return 1;
            }
            error = FASTPNG_NEED_MORE_DATA;
            for (int k = 1; k <= pieces && error == FASTPNG_NEED_MORE_DATA; ++k) {
                fastpng_progress progress;
                error = fastpng_decoder_update(decoder, png, size * k / pieces, k == pieces, FASTPNG_RGBA, pixels, (size_t)width * 4, &progress);
                if (r == 0 && pieces <= 32)
                    printf("%s%u/%u/%u", k == 1 ? "after each piece, rows final/shown/passes done: " : " ", progress.rows_final, progress.rows_shown, progress.passes_done);
            }
            if (r == 0 && pieces <= 32)
                printf("\n");
            fastpng_decoder_destroy(decoder);
        } else {
            error = fastpng_decode(png, size, FASTPNG_RGBA, pixels, (size_t)width * 4, &options);
        }
        double const elapsed = now_ms() - start;
        if (error) {
            fprintf(stderr, "%s: %s\n", input, fastpng_error_string(error));
            return 1;
        }
        best = elapsed < best ? elapsed : best;
    }
    double const megapixels = (double)width * height / 1e6;
    printf("%s: %ux%u, %.2f ms (best of %d%s), %.0f MPix/s\n", input, width, height, best, repeats, pieces > 0 ? ", in pieces" : "", megapixels / (best / 1e3));

    if (output) {
        FILE* f = fopen(output, "wb");
        if (!f) {
            fprintf(stderr, "can't write %s\n", output);
            return 1;
        }
        fprintf(f, "P7\nWIDTH %u\nHEIGHT %u\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", width, height);
        fwrite(pixels, 4, (size_t)width * height, f);
        fclose(f);
    }
    free(pixels);
    free(png);
    return 0;
}
