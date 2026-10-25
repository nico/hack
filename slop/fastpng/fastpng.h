// fastpng: a fast PNG decoder in one C file. See README.md.
#ifndef FASTPNG_H
#define FASTPNG_H

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

// Flags for fastpng_options: which checksums to verify. With neither (0), none are computed.
enum {
    FASTPNG_CHECK_CRC = 1,      // every chunk's CRC-32, up to IEND
    FASTPNG_CHECK_ADLER32 = 2,  // the zlib stream's Adler-32 of the inflated data
    FASTPNG_CHECK_ALL = FASTPNG_CHECK_CRC | FASTPNG_CHECK_ADLER32,
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

char const* fastpng_error_string(int error);

#ifdef __cplusplus
}
#endif

#endif
