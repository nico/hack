// PNG decode benchmark: fastpng vs wuffs, libpng, stb_image, fpng, lodepng and the Rust png crate
// (with -DHAVE_RUST_PNG; see build.sh), whole image to
// 8-bit RGBA, in-process, best of N runs, each into newly allocated output. Every decoder's pixels
// are compared against wuffs' (fully transparent pixels match whatever their color).
// See build.sh for how it's built, and README.md for how to run it.

#include "fastpng.h"
#include "fpng.h"
#include "lodepng.h"
#include "stb_image.h"
#include <png.h>

#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__ZLIB
#define WUFFS_CONFIG__MODULE__PNG
#include "wuffs-v0.4.c"

#include <algorithm>
#include <math.h>
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

struct Image {
    int w { 0 }, h { 0 };
    std::vector<uint8_t> rgba;
};

typedef bool (*Decoder)(uint8_t const*, size_t, Image&);

static bool decode_fastpng_with(uint8_t const* data, size_t size, Image& out, unsigned flags)
{
    uint32_t w, h;
    fastpng_options const options = { flags, 0 };
    if (fastpng_get_size(data, size, &options, &w, &h))
        return false;
    out.w = w;
    out.h = h;
    out.rgba.resize((size_t)w * h * 4);
    return fastpng_decode(data, size, FASTPNG_RGBA, out.rgba.data(), (size_t)w * 4, &options) == FASTPNG_OK;
}

static bool decode_fastpng(uint8_t const* data, size_t size, Image& out) { return decode_fastpng_with(data, size, out, 0); }
static bool decode_fastpng_checked(uint8_t const* data, size_t size, Image& out) { return decode_fastpng_with(data, size, out, FASTPNG_CHECK_ALL); }

static bool decode_wuffs_with(uint8_t const* data, size_t size, Image& out, bool ignore_checksum)
{
    static wuffs_png__decoder* dec = (wuffs_png__decoder*)malloc(sizeof__wuffs_png__decoder());
    if (wuffs_png__decoder__initialize(dec, sizeof__wuffs_png__decoder(), WUFFS_VERSION, WUFFS_INITIALIZE__LEAVE_INTERNAL_BUFFERS_UNINITIALIZED).repr)
        return false;
    if (ignore_checksum)
        wuffs_png__decoder__set_quirk(dec, WUFFS_BASE__QUIRK_IGNORE_CHECKSUM, 1);
    wuffs_base__image_config ic;
    wuffs_base__io_buffer src = wuffs_base__ptr_u8__reader(const_cast<uint8_t*>(data), size, true);
    if (wuffs_png__decoder__decode_image_config(dec, &ic, &src).repr)
        return false;
    out.w = wuffs_base__pixel_config__width(&ic.pixcfg);
    out.h = wuffs_base__pixel_config__height(&ic.pixcfg);
    wuffs_base__pixel_config__set(&ic.pixcfg, WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL, WUFFS_BASE__PIXEL_SUBSAMPLING__NONE, out.w, out.h);
    out.rgba.resize((size_t)out.w * out.h * 4);
    wuffs_base__pixel_buffer pb;
    if (wuffs_base__pixel_buffer__set_from_slice(&pb, &ic.pixcfg, wuffs_base__make_slice_u8(out.rgba.data(), out.rgba.size())).repr)
        return false;
    uint64_t const workbuf_len = wuffs_png__decoder__workbuf_len(dec).max_incl;
    std::vector<uint8_t> workbuf(workbuf_len ? workbuf_len : 1);
    return !wuffs_png__decoder__decode_frame(dec, &pb, &src, WUFFS_BASE__PIXEL_BLEND__SRC, wuffs_base__make_slice_u8(workbuf.data(), workbuf_len), nullptr).repr;
}

static bool decode_wuffs(uint8_t const* data, size_t size, Image& out) { return decode_wuffs_with(data, size, out, false); }
static bool decode_wuffs_no_checksum(uint8_t const* data, size_t size, Image& out) { return decode_wuffs_with(data, size, out, true); }

static bool decode_libpng(uint8_t const* data, size_t size, Image& out)
{
    png_image image;
    memset(&image, 0, sizeof image);
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, data, size))
        return false;
    image.format = PNG_FORMAT_RGBA;
    out.w = image.width;
    out.h = image.height;
    out.rgba.resize(PNG_IMAGE_SIZE(image));
    bool const ok = png_image_finish_read(&image, nullptr, out.rgba.data(), 0, nullptr);
    png_image_free(&image);
    return ok;
}

static bool decode_stb(uint8_t const* data, size_t size, Image& out)
{
    int w, h, channels;
    uint8_t* pixels = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
    if (!pixels)
        return false;
    out.w = w;
    out.h = h;
    out.rgba.assign(pixels, pixels + (size_t)w * h * 4);
    stbi_image_free(pixels);
    return true;
}

// fpng decodes only files fpng wrote (see --fpng-reencode).
static bool decode_fpng(uint8_t const* data, size_t size, Image& out)
{
    uint32_t w, h, channels;
    if (fpng::fpng_decode_memory(data, (uint32_t)size, out.rgba, w, h, channels, 4) != fpng::FPNG_DECODE_SUCCESS)
        return false;
    out.w = w;
    out.h = h;
    return true;
}

// Incremental decoding: each decoder gets the file in `pieces` equal pieces, as if it were
// arriving over the network, and resumes where it stopped each time.
static int pieces = 64;
// With --preview: stop after this many pieces, leaving the partial image (buffers start zeroed).
static int stop_after = 0;

static size_t end_of_piece(size_t size, int k)
{
    return size * k / pieces;
}

static bool incremental_fastpng(uint8_t const* data, size_t size, Image& out)
{
    uint32_t w, h;
    if (fastpng_get_size(data, size, NULL, &w, &h))
        return false;
    out.w = w;
    out.h = h;
    out.rgba.resize((size_t)w * h * 4);
    fastpng_options const options = { FASTPNG_FILL_INTERLACE_PASSES, 0 };
    fastpng_decoder* decoder = fastpng_decoder_create(&options);
    int status = FASTPNG_NEED_MORE_DATA;
    for (int k = 1; k <= pieces && status == FASTPNG_NEED_MORE_DATA && (!stop_after || k <= stop_after); ++k)
        status = fastpng_decoder_update(decoder, data, end_of_piece(size, k), k == pieces, FASTPNG_RGBA, out.rgba.data(), (size_t)w * 4, NULL);
    fastpng_decoder_destroy(decoder);
    return status == FASTPNG_OK || (stop_after && status == FASTPNG_NEED_MORE_DATA);
}

// libpng's progressive reader: it gets each new piece, and calls back with rows.
struct LibpngProgressive {
    Image* out;
    bool done;
};

static void libpng_info(png_structp png, png_infop info)
{
    auto* state = (LibpngProgressive*)png_get_progressive_ptr(png);
    png_set_expand(png);
    png_set_strip_16(png);
    png_set_gray_to_rgb(png);
    png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);
    png_set_interlace_handling(png);
    png_read_update_info(png, info);
    state->out->w = png_get_image_width(png, info);
    state->out->h = png_get_image_height(png, info);
    state->out->rgba.assign((size_t)state->out->w * state->out->h * 4, 0);
}

static void libpng_row(png_structp png, png_bytep row, png_uint_32 y, int)
{
    auto* state = (LibpngProgressive*)png_get_progressive_ptr(png);
    if (row)
        png_progressive_combine_row(png, &state->out->rgba[(size_t)y * state->out->w * 4], row);
}

static void libpng_end(png_structp png, png_infop)
{
    ((LibpngProgressive*)png_get_progressive_ptr(png))->done = true;
}

static bool incremental_libpng(uint8_t const* data, size_t size, Image& out)
{
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    LibpngProgressive state = { &out, false };
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        return false;
    }
    png_set_progressive_read_fn(png, &state, libpng_info, libpng_row, libpng_end);
    size_t have = 0;
    for (int k = 1; k <= pieces && !state.done && (!stop_after || k <= stop_after); ++k) {
        size_t const next = end_of_piece(size, k);
        png_process_data(png, info, const_cast<uint8_t*>(data) + have, next - have);
        have = next;
    }
    png_destroy_read_struct(&png, &info, nullptr);
    return state.done || stop_after;
}

// wuffs suspends when its input buffer runs out ("short read") and continues when it has more.
static bool incremental_wuffs(uint8_t const* data, size_t size, Image& out)
{
    static wuffs_png__decoder* dec = (wuffs_png__decoder*)malloc(sizeof__wuffs_png__decoder());
    if (wuffs_png__decoder__initialize(dec, sizeof__wuffs_png__decoder(), WUFFS_VERSION, WUFFS_INITIALIZE__LEAVE_INTERNAL_BUFFERS_UNINITIALIZED).repr)
        return false;
    wuffs_base__io_buffer src = wuffs_base__ptr_u8__reader(const_cast<uint8_t*>(data), size, false);
    wuffs_base__image_config ic;
    wuffs_base__pixel_buffer pb;
    std::vector<uint8_t> workbuf;
    bool have_config = false;
    for (int k = 1; k <= pieces; ++k) {
        if (stop_after && k > stop_after)
            return have_config;
        src.meta.wi = end_of_piece(size, k);
        src.meta.closed = k == pieces;
        if (!have_config) {
            wuffs_base__status const status = wuffs_png__decoder__decode_image_config(dec, &ic, &src);
            if (status.repr == wuffs_base__suspension__short_read)
                continue;
            if (status.repr)
                return false;
            have_config = true;
            out.w = wuffs_base__pixel_config__width(&ic.pixcfg);
            out.h = wuffs_base__pixel_config__height(&ic.pixcfg);
            wuffs_base__pixel_config__set(&ic.pixcfg, WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL, WUFFS_BASE__PIXEL_SUBSAMPLING__NONE, out.w, out.h);
            out.rgba.resize((size_t)out.w * out.h * 4);
            if (wuffs_base__pixel_buffer__set_from_slice(&pb, &ic.pixcfg, wuffs_base__make_slice_u8(out.rgba.data(), out.rgba.size())).repr)
                return false;
            workbuf.resize(wuffs_png__decoder__workbuf_len(dec).max_incl + 1);
        }
        wuffs_base__status const status = wuffs_png__decoder__decode_frame(dec, &pb, &src, WUFFS_BASE__PIXEL_BLEND__SRC, wuffs_base__make_slice_u8(workbuf.data(), workbuf.size() - 1), nullptr);
        if (status.repr == wuffs_base__suspension__short_read)
            continue;
        return !status.repr;
    }
    return false;
}

#ifdef HAVE_RUST_PNG
// The png crate, through rust-png/ (what Chromium's PNG decoding uses).
extern "C" int rustpng_get_size(uint8_t const* data, size_t size, uint32_t* width, uint32_t* height);
extern "C" int rustpng_decode(uint8_t const* data, size_t size, bool ignore_checksums, uint8_t* rgba, size_t rgba_size);

static bool decode_rust_png_with(uint8_t const* data, size_t size, Image& out, bool ignore_checksums)
{
    uint32_t w, h;
    if (rustpng_get_size(data, size, &w, &h))
        return false;
    out.w = w;
    out.h = h;
    out.rgba.resize((size_t)w * h * 4);
    return rustpng_decode(data, size, ignore_checksums, out.rgba.data(), out.rgba.size()) == 0;
}

// The crate's defaults: chunk CRCs checked, the zlib Adler-32 not.
static bool decode_rust_png(uint8_t const* data, size_t size, Image& out) { return decode_rust_png_with(data, size, out, false); }
static bool decode_rust_png_no_checksums(uint8_t const* data, size_t size, Image& out) { return decode_rust_png_with(data, size, out, true); }

// Its reader resumes after running out of input; it gets each new piece.
extern "C" void* rustpng_stream_create(bool ignore_checksums);
extern "C" int rustpng_stream_update(void* stream, uint8_t const* data, size_t size, bool complete, uint8_t* rgba, size_t rgba_size);
extern "C" void rustpng_stream_snapshot(void* stream, uint8_t* rgba, size_t rgba_size);
extern "C" void rustpng_stream_destroy(void* stream);

static bool incremental_rust_png(uint8_t const* data, size_t size, Image& out)
{
    uint32_t w, h;
    if (rustpng_get_size(data, size, &w, &h))
        return false;
    out.w = w;
    out.h = h;
    out.rgba.resize((size_t)w * h * 4);
    void* stream = rustpng_stream_create(false);
    int status = 1;
    size_t have = 0;
    for (int k = 1; k <= pieces && status == 1 && (!stop_after || k <= stop_after); ++k) {
        size_t const next = end_of_piece(size, k);
        status = rustpng_stream_update(stream, data + have, next - have, k == pieces, out.rgba.data(), out.rgba.size());
        have = next;
    }
    if (stop_after && status == 1)
        rustpng_stream_snapshot(stream, out.rgba.data(), out.rgba.size());
    rustpng_stream_destroy(stream);
    return status == 0 || (stop_after && status == 1);
}
#endif

static bool decode_lodepng(uint8_t const* data, size_t size, Image& out)
{
    unsigned w, h;
    if (lodepng::decode(out.rgba, w, h, data, size, LCT_RGBA, 8))
        return false;
    out.w = w;
    out.h = h;
    return true;
}

struct DecoderInfo {
    char const* name;
    Decoder decode;
};

static DecoderInfo const decoders[] = {
    { "fastpng", decode_fastpng },
    { "fp-chk", decode_fastpng_checked },
    { "wuffs", decode_wuffs },
    { "wuffs-nc", decode_wuffs_no_checksum },
    { "libpng", decode_libpng },
    { "stb", decode_stb },
    { "fpng", decode_fpng },
    { "lodepng", decode_lodepng },
#ifdef HAVE_RUST_PNG
    { "png-rs", decode_rust_png },
    { "png-rs-nc", decode_rust_png_no_checksums },
#endif
};
// The decoders that can resume, decoding in pieces (--incremental), each checking what it does by
// default: fastpng nothing, libpng and wuffs CRCs and the Adler-32, the png crate CRCs.
static DecoderInfo const incremental_decoders[] = {
    { "fastpng", incremental_fastpng },
    { "libpng", incremental_libpng },
    { "wuffs", incremental_wuffs },
#ifdef HAVE_RUST_PNG
    { "png-rs", incremental_rust_png },
#endif
};
static constexpr size_t incremental_decoder_count = sizeof(incremental_decoders) / sizeof(incremental_decoders[0]);
static constexpr size_t max_decoder_count = sizeof(decoders) / sizeof(decoders[0]);
static DecoderInfo const* table = decoders;
static size_t decoder_count = max_decoder_count;

static size_t count_diffs(Image const& a, Image const& b)
{
    if (a.w != b.w || a.h != b.h)
        return SIZE_MAX;
    size_t n = 0;
    for (size_t i = 0; i < a.rgba.size(); i += 4)
        n += memcmp(&a.rgba[i], &b.rgba[i], 4) != 0 && !(a.rgba[i + 3] == 0 && b.rgba[i + 3] == 0);
    return n;
}

static double now_ms()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static double best_ms(int runs, Decoder decode, uint8_t const* data, size_t size)
{
    double best = 1e30;
    for (int i = 0; i < runs; ++i) {
        Image fresh;
        double const start = now_ms();
        if (!decode(data, size, fresh))
            return -1;
        best = std::min(best, now_ms() - start);
    }
    return best;
}

static bool read_file(char const* path, std::vector<uint8_t>& bytes)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    bytes.resize(ftell(f));
    fseek(f, 0, SEEK_SET);
    bool const ok = fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
    fclose(f);
    return ok;
}

// Decodes with wuffs and encodes with fpng (RGB if opaque, else RGBA).
static bool fpng_reencode(std::vector<uint8_t>& bytes)
{
    Image source;
    if (!decode_wuffs(bytes.data(), bytes.size(), source))
        return false;
    bool opaque = true;
    for (size_t p = 3; p < source.rgba.size() && opaque; p += 4)
        opaque = source.rgba[p] == 0xff;
    std::vector<uint8_t> pixels;
    for (size_t p = 0; p < source.rgba.size(); p += 4)
        pixels.insert(pixels.end(), source.rgba.begin() + p, source.rgba.begin() + p + (opaque ? 3 : 4));
    std::vector<uint8_t> encoded;
    if (!fpng::fpng_encode_image_to_memory(pixels.data(), source.w, source.h, opaque ? 3 : 4, encoded))
        return false;
    bytes = std::move(encoded);
    return true;
}

int main(int argc, char** argv)
{
    int runs = 5;
    bool reencode = false;
    bool check_only = false;
    std::vector<bool> enabled(max_decoder_count, true);
    std::string only;
    char const* preview_dir = nullptr;
    int first = 1;
    for (; first < argc && argv[first][0] == '-'; ++first) {
        if (!strcmp(argv[first], "-n") && first + 1 < argc) {
            runs = atoi(argv[++first]);
        } else if (!strcmp(argv[first], "--incremental") && first + 1 < argc) {
            pieces = atoi(argv[++first]);
            table = incremental_decoders;
            decoder_count = incremental_decoder_count;
        } else if (!strcmp(argv[first], "--preview") && first + 1 < argc) {
            stop_after = atoi(argv[++first]);
        } else if (!strcmp(argv[first], "--preview-dir") && first + 1 < argc) {
            preview_dir = argv[++first];
        } else if (!strcmp(argv[first], "--fpng-reencode")) {
            reencode = true;
        } else if (!strcmp(argv[first], "--check")) {
            check_only = true;
        } else if (!strcmp(argv[first], "--only") && first + 1 < argc) {
            only = std::string(",") + argv[++first] + ",";
        } else {
            fprintf(stderr, "usage: %s [-n runs] [--incremental pieces [--preview piece [--preview-dir dir]]] [--fpng-reencode] [--check] [--only fastpng,wuffs,...] file.png...\n", argv[0]);
            return 1;
        }
    }
    for (size_t d = 0; d < decoder_count && !only.empty(); ++d)
        enabled[d] = only.find(std::string(",") + table[d].name + ",") != std::string::npos;
    fpng::fpng_init();

    if (check_only) {
        // Correctness only: which files fastpng decodes differently from wuffs (or fails on), with
        // checksums checked like wuffs does, and without (where only corrupt checksums may differ).
        int bad = 0, total = 0;
        for (int i = first; i < argc; ++i) {
            std::vector<uint8_t> bytes;
            if (!read_file(argv[i], bytes))
                continue;
            Image reference, ours, checked;
            bool const reference_ok = decode_wuffs(bytes.data(), bytes.size(), reference);
            bool const ours_ok = decode_fastpng(bytes.data(), bytes.size(), ours);
            bool const checked_ok = decode_fastpng_checked(bytes.data(), bytes.size(), checked);
            ++total;
            if (reference_ok != checked_ok || (checked_ok && count_diffs(checked, reference))) {
                ++bad;
                printf("%s: wuffs %s, fastpng with checksums %s, %zd pixels differ\n", argv[i], reference_ok ? "ok" : "fails", checked_ok ? "ok" : "fails",
                    reference_ok && checked_ok ? (ssize_t)count_diffs(checked, reference) : -1);
            }
            if (checked_ok && (!ours_ok || count_diffs(ours, checked))) {
                ++bad;
                printf("%s: fastpng without checksums decodes differently from with them\n", argv[i]);
            } else if (!checked_ok && ours_ok) {
                printf("%s: (only a checksum is wrong: fastpng decodes it without checking)\n", argv[i]);
            }
        }
        printf("%d of %d files decode differently from wuffs (with checksums checked)\n", bad, total);
        return bad != 0;
    }

    if (stop_after) {
        // What each decoder that can resume shows after `stop_after` of `pieces` pieces: the share
        // of pixels already final, and how close the whole partial image is to the final one.
        table = incremental_decoders;
        decoder_count = incremental_decoder_count;
        printf("%-30s %5s", "file", "inter");
        for (size_t d = 0; d < decoder_count; ++d) {
            if (enabled[d])
                printf(" %18s", table[d].name);
        }
        printf("   (after %d of %d pieces: pixels final, PSNR against the final image)\n", stop_after, pieces);
        std::vector<double> final_sum(decoder_count, 0), psnr_sum(decoder_count, 0);
        int files = 0;
        for (int i = first; i < argc; ++i) {
            std::vector<uint8_t> bytes;
            Image reference;
            if (!read_file(argv[i], bytes) || !decode_wuffs(bytes.data(), bytes.size(), reference))
                continue;
            char const* name = strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i];
            printf("%-30.30s %5s", name, bytes.size() > 28 && bytes[28] ? "yes" : "");
            ++files;
            for (size_t d = 0; d < decoder_count; ++d) {
                if (!enabled[d])
                    continue;
                Image image;
                if (!table[d].decode(bytes.data(), bytes.size(), image) || image.rgba.size() != reference.rgba.size()) {
                    printf(" %18s", "(nothing yet)");
                    psnr_sum[d] += 0;
                    continue;
                }
                size_t final_pixels = 0;
                double squared_error = 0;
                for (size_t p = 0; p < image.rgba.size(); p += 4) {
                    final_pixels += memcmp(&image.rgba[p], &reference.rgba[p], 4) == 0;
                    for (int c = 0; c < 4; ++c) {
                        double const e = (double)image.rgba[p + c] - reference.rgba[p + c];
                        squared_error += e * e;
                    }
                }
                double const share = 100.0 * final_pixels / (image.rgba.size() / 4);
                double const mse = squared_error / image.rgba.size();
                double const psnr = mse ? 10 * log10(255.0 * 255.0 / mse) : 99;
                printf("   %5.1f%% %6.1f dB", share, psnr);
                final_sum[d] += share;
                psnr_sum[d] += psnr;
                if (preview_dir) {
                    std::string const path = std::string(preview_dir) + "/" + name + "." + table[d].name + ".pam";
                    if (FILE* f = fopen(path.c_str(), "wb")) {
                        fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", image.w, image.h);
                        fwrite(image.rgba.data(), 1, image.rgba.size(), f);
                        fclose(f);
                    }
                }
            }
            printf("\n");
        }
        printf("%-30s %5s", "MEAN", "");
        for (size_t d = 0; d < decoder_count; ++d) {
            if (enabled[d])
                printf("   %5.1f%% %6.1f dB", final_sum[d] / files, psnr_sum[d] / files);
        }
        printf("\n");
        return 0;
    }

    printf("%-30s %6s %7s", "file", "MPix", "KB");
    for (size_t d = 0; d < decoder_count; ++d) {
        if (enabled[d])
            printf(" %8s", table[d].name);
    }
    printf("   (ms, best of %d; '-' = can't decode; '!' = pixels differ from wuffs)", runs);
    if (table == incremental_decoders)
        printf(" decoding in %d pieces", pieces);
    printf("\n");

    std::vector<double> totals(decoder_count, 0);
    std::vector<double> fastpng_where_decoded(decoder_count, 0);
    for (int i = first; i < argc; ++i) {
        std::vector<uint8_t> bytes;
        if (!read_file(argv[i], bytes)) {
            fprintf(stderr, "can't read %s\n", argv[i]);
            continue;
        }
        if (reencode && !fpng_reencode(bytes)) {
            fprintf(stderr, "%s: can't re-encode with fpng\n", argv[i]);
            continue;
        }
        Image reference;
        if (!decode_wuffs(bytes.data(), bytes.size(), reference)) {
            fprintf(stderr, "%s: wuffs can't decode it\n", argv[i]);
            continue;
        }
        char const* name = strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i];
        printf("%-30.30s %6.2f %7zu", name, reference.w * (double)reference.h / 1e6, bytes.size() / 1024);
        double const fastpng_ms = best_ms(runs, table[0].decode, bytes.data(), bytes.size());
        for (size_t d = 0; d < decoder_count; ++d) {
            if (!enabled[d])
                continue;
            Image image;
            bool const ok = table[d].decode(bytes.data(), bytes.size(), image);
            double const ms = !ok ? -1 : d == 0 ? fastpng_ms : best_ms(runs, table[d].decode, bytes.data(), bytes.size());
            if (ms < 0) {
                printf(" %8s", "-");
                continue;
            }
            printf(" %7.2f%c", ms, count_diffs(image, reference) ? '!' : ' ');
            totals[d] += ms;
            fastpng_where_decoded[d] += fastpng_ms;
        }
        printf("\n");
        fflush(stdout);
    }
    printf("%-30s %6s %7s", "TOTAL", "", "");
    for (size_t d = 0; d < decoder_count; ++d) {
        if (enabled[d])
            printf(" %8.2f", totals[d]);
    }
    printf("\n%-30s %6s %7s", "fastpng's time / theirs", "", "");
    for (size_t d = 0; d < decoder_count; ++d) {
        if (enabled[d])
            printf(" %8.2f", totals[d] > 0 ? fastpng_where_decoded[d] / totals[d] : 0);
    }
    printf("\n");
}
