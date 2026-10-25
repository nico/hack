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
};

// Reads the image size from the header. Returns FASTPNG_OK or an error.
int fastpng_get_size(void const* png, size_t png_size, uint32_t* width, uint32_t* height);

// Decodes the image (the first frame of an APNG) into `pixels`, `stride` bytes per row, 4 bytes
// per pixel in `format`. Returns FASTPNG_OK or an error; on error `pixels` may be partly written.
// Gamma, color profiles and checksums are ignored.
int fastpng_decode(void const* png, size_t png_size, fastpng_format format, void* pixels, size_t stride);

// Like fastpng_decode(), into a malloc()ed buffer of width * height * 4 bytes, or NULL on error
// (with the error in *error when it's not NULL).
uint8_t* fastpng_decode_alloc(void const* png, size_t png_size, fastpng_format format, uint32_t* width, uint32_t* height, int* error);

char const* fastpng_error_string(int error);

#ifdef __cplusplus
}
#endif

#endif
