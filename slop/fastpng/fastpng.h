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

// Flags for fastpng_decode(): which checksums to verify. With neither (0), none are computed.
enum {
    FASTPNG_CHECK_CRC = 1,      // every chunk's CRC-32, up to IEND
    FASTPNG_CHECK_ADLER32 = 2,  // the zlib stream's Adler-32 of the inflated data
    FASTPNG_CHECK_ALL = FASTPNG_CHECK_CRC | FASTPNG_CHECK_ADLER32,
};

// Reads the image size from the header. Returns FASTPNG_OK or an error.
int fastpng_get_size(void const* png, size_t png_size, uint32_t* width, uint32_t* height);

// Decodes the image (the first frame of an APNG) into `pixels`, `stride` bytes per row, 4 bytes
// per pixel in `format`, verifying the checksums `flags` asks for. Returns FASTPNG_OK or an error;
// on error `pixels` may be partly written. Gamma and color profiles are ignored.
int fastpng_decode(void const* png, size_t png_size, fastpng_format format, void* pixels, size_t stride, unsigned flags);

// Like fastpng_decode(), into a malloc()ed buffer of width * height * 4 bytes, or NULL on error
// (with the error in *error when it's not NULL).
uint8_t* fastpng_decode_alloc(void const* png, size_t png_size, fastpng_format format, unsigned flags, uint32_t* width, uint32_t* height, int* error);

char const* fastpng_error_string(int error);

#ifdef __cplusplus
}
#endif

#endif
