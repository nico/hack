// fastpng: a fast PNG decoder in one C file. See README.md.
#ifndef FASTPNG_H
#define FASTPNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The byte order of each decoded pixel. 16-bit samples keep their high byte.
typedef enum {
    FASTPNG_RGBA = 0,
    FASTPNG_BGRA = 1,
} fastpng_format;

enum {
    FASTPNG_OK = 0,
    FASTPNG_ERROR_NOT_PNG = -1,
    FASTPNG_ERROR_BAD_HEADER = -2,      // IHDR missing, malformed or unsupported
    FASTPNG_ERROR_TOO_LARGE = -3,       // the image wouldn't fit in memory
    FASTPNG_ERROR_TRUNCATED = -4,       // the file or the image data ends early
    FASTPNG_ERROR_BAD_ZLIB = -5,        // invalid zlib or deflate data
    FASTPNG_ERROR_BAD_PALETTE = -6,     // missing PLTE, or an index past its end
    FASTPNG_ERROR_OUT_OF_MEMORY = -7,
    FASTPNG_ERROR_BAD_ARGUMENT = -8,
    FASTPNG_ERROR_BAD_CHECKSUM = -9,    // a chunk's CRC or the zlib Adler-32 is wrong (when checked)
};

// Flags for fastpng_options. Which checksums to verify (with neither, none are computed), and for
// decoding an interlaced image as it arrives, whether to show a coarse version early.
enum {
    FASTPNG_CHECK_CRC = 1,      // every chunk's CRC-32, up to IEND
    FASTPNG_CHECK_ADLER32 = 2,  // the zlib stream's Adler-32 of the inflated data
    FASTPNG_CHECK_ALL = FASTPNG_CHECK_CRC | FASTPNG_CHECK_ADLER32,
    // Spread each interlace pass's pixels over the block of pixels they stand for (8x8 for the
    // first pass, down to single pixels for the last), so that while an interlaced image arrives,
    // its pixels show it coarse first and sharper with each pass. Only does anything while the
    // image is incomplete; the final pixels are the same either way.
    FASTPNG_FILL_INTERLACE_PASSES = 4,
};

// The default limit on an image's pixels: 2^28, about 268 megapixels (16384 x 16384), which is
// 1 GiB of output. Decoding needs the output plus at most twice that again (16-bit RGBA's
// inflated rows), and a copy of the image data when it's in several chunks.
#define FASTPNG_DEFAULT_MAX_PIXELS ((uint64_t)1 << 28)

typedef struct {
    unsigned flags;      // FASTPNG_CHECK_*
    uint64_t max_pixels; // larger images fail with FASTPNG_ERROR_TOO_LARGE; 0 means the default,
                         // UINT64_MAX no limit beyond what fits in memory
} fastpng_options;
// Every function taking options also takes NULL, meaning { 0, FASTPNG_DEFAULT_MAX_PIXELS }.

// Reads the image size from the header. Returns FASTPNG_OK or an error, FASTPNG_ERROR_TOO_LARGE
// when the image is over the limit in `options`; after FASTPNG_OK, width * height * 4 fits in a
// size_t, so a caller can allocate the pixels with it.
int fastpng_get_size(void const* png, size_t png_size, fastpng_options const* options, uint32_t* width, uint32_t* height);

// Decodes the image (the first frame of an APNG) into `pixels`, `stride` bytes per row, 4 bytes
// per pixel in `format`, with the checksums and limit in `options`. Returns FASTPNG_OK or an
// error; on error `pixels` may be partly written. Gamma and color profiles are ignored.
int fastpng_decode(void const* png, size_t png_size, fastpng_format format, void* pixels, size_t stride, fastpng_options const* options);

// Like fastpng_decode(), into a malloc()ed buffer of width * height * 4 bytes, or NULL on error
// (with the error in *error when it's not NULL).
uint8_t* fastpng_decode_alloc(void const* png, size_t png_size, fastpng_format format, fastpng_options const* options, uint32_t* width, uint32_t* height, int* error);

// Decoding a file as it arrives. Create a decoder, then call fastpng_decoder_update() with all of
// the file received so far, each time with more (the same bytes, and more after them; the buffer
// may move), and `complete` once that's all of it. Each call decodes what the new data allows,
// without redoing earlier work, into the same `pixels` (sized from fastpng_get_size(), which needs
// only the first 33 bytes) and returns FASTPNG_NEED_MORE_DATA until the image is done (FASTPNG_OK)
// or fails, saying in *progress what can be shown so far. Once it has returned anything but
// FASTPNG_NEED_MORE_DATA, a decoder keeps returning that. The result is the one-step decode's,
// except that in a file with several problems the error may name another one.
#define FASTPNG_NEED_MORE_DATA 1
typedef struct fastpng_decoder fastpng_decoder;

typedef struct {
    // Rows from the top whose pixels are final. A non-interlaced image's come in from the top as
    // their data arrives; an interlaced image's are final only at the end.
    uint32_t rows_final;
    // Rows from the top worth showing now: rows_final, and for an interlaced image decoded with
    // FASTPNG_FILL_INTERLACE_PASSES, the rows its passes so far cover (all of them once the first
    // pass is done).
    uint32_t rows_shown;
    // Interlace passes done (0 to 7); a non-interlaced image counts as one pass.
    unsigned passes_done;
} fastpng_progress;

fastpng_decoder* fastpng_decoder_create(fastpng_options const* options);
int fastpng_decoder_update(fastpng_decoder* decoder, void const* png, size_t png_size, bool complete, fastpng_format format, void* pixels, size_t stride, fastpng_progress* progress);
void fastpng_decoder_destroy(fastpng_decoder* decoder);

// Like fastpng_decoder_update(), with the file received so far in pieces, in order: say, the
// buffers it arrived in. Each call gets all of it again (the pieces may move, be split or joined
// differently, and the last may grow). The image data is read in place; only the bytes around
// where pieces meet are copied together.
typedef struct {
    void const* data;
    size_t size;
} fastpng_span;
int fastpng_decoder_update_spans(fastpng_decoder* decoder, fastpng_span const* spans, size_t span_count, bool complete, fastpng_format format, void* pixels, size_t stride, fastpng_progress* progress);

char const* fastpng_error_string(int error);

#ifdef __cplusplus
}
#endif

#endif
