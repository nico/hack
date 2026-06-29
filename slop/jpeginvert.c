/* jpeginvert.c -- losslessly invert one stored component of a baseline JPEG.
 *
 * Idea by thakis@chromium.org, code by an LLM.
 *
 * Inverts ("photographic negative") a single stored component -- Y, Cb,
 * or Cr -- of a baseline JPEG and writes a new JPEG, WITHOUT decoding to
 * pixels and re-encoding. No dependencies (no libjpeg).
 *
 * The whole operation happens on the quantized DCT coefficients inside
 * the entropy-coded scan: it negates every quantized coefficient of the
 * chosen component (the DC differential plus all 63 AC coefficients of
 * every 8x8 block). Inverting a component in the pixel domain is exactly
 * a negation of its coefficients, because the DCT is linear and JPEG's
 * level shift (the -128 applied before the forward DCT) is just an
 * additive constant.
 *
 * =====================================================================
 * WHY 256 - p, AND THE ENDPOINTS:  255 -> 1  AND  0 -> 255
 * =====================================================================
 * The "textbook" photographic negative is  p -> 255 - p.  Written in the
 * level-shifted domain (s = p - 128), 255 - p becomes  -s - 1: a clean
 * negation (-s) PLUS a constant -1. That "-1" is a uniform offset across
 * the whole 8x8 block, so it lands entirely on the DC term -- concretely
 * DC -= 8 (the forward DCT of a constant-1 block is DC = 8, all AC = 0).
 * Subtracting 8 from a quantized DC value is exactly representable only
 * if the DC quantization step divides 8 (step in {1,2,4,8}). Standard
 * JPEG tables use DC step 16 (luma) / 17 (chroma), which do NOT divide 8,
 * so the wanted value falls exactly between two representable levels:
 * true 255 - p is therefore NOT achievable losslessly on normal files.
 *
 * So we drop the "-1" and negate ALL coefficients, DC included. That
 * realises the  p -> 256 - p  inversion. In decoded-pixel terms, for an
 * interior sample:
 *
 *       p ->  256 - p        (128 -> 128,  200 -> 56,  1 -> 255)
 *
 * The two endpoints are the visible signature of choosing 256 - p over
 * 255 - p, and are worth stating explicitly:
 *
 *   * WHITE:  p = 255  ->  256 - 255 = 1   (NOT 0).
 *     White maps to 1, one level short of true black. This deliberate
 *     off-by-one is the entire reason the operation is lossless without
 *     re-encoding: it is what lets us skip the un-representable DC -= 8
 *     adjustment, so nothing is ever requantized.
 *
 *   * BLACK:  p = 0    ->  256, which a DECODER CLAMPS to 255.
 *     256 is outside [0,255], so any conformant decoder clamps it back
 *     to 255 -- the same value a true negative would give at this end.
 *     Consequently both p=0 and p=1 decode to 255, and the level 0 is
 *     never produced: 256 - p is not a clean permutation of 0..255.
 *
 *   (These endpoint/clamp remarks describe what a DECODER does with our
 *   output. This program never decodes to pixels and never clamps; it
 *   only rewrites coefficients in the bitstream.)
 *
 * =====================================================================
 * HOW IT STAYS LOSSLESS / WHY THE SAME HUFFMAN TABLES STILL WORK
 * =====================================================================
 * Negating a quantized coefficient  q -> -q :
 *   - preserves its magnitude category ("size" s), since |q| == |-q|, so
 *     the (run, size) Huffman SYMBOL is unchanged -- the existing DHT
 *     tables stay valid and every symbol keeps the same bit length;
 *   - preserves the zero / nonzero run structure, since q is zero iff -q
 *     is, so EOB / ZRL placement is unchanged;
 *   - under JPEG's RECEIVE/EXTEND magnitude coding, is EXACTLY the
 *     bitwise complement of the s mantissa bits.
 * DC is differentially coded (DPCM), but negating each coded difference
 * negates the running DC value consistently, so the same per-coefficient
 * rule covers the DC token and NO predictor state is needed anywhere.
 *
 * Net effect: every header byte and every untouched component is copied
 * bit-for-bit; for the target component only the mantissa bits flip.
 * (Output byte-stuffing, 0xFF -> 0xFF 0x00, is re-derived, because
 * flipped bits can create or remove 0xFF bytes.)
 *
 * =====================================================================
 * EXACTNESS: WHEN RUNNING THIS TWICE IS A PERFECT INVERSE, AND WHEN NOT
 * =====================================================================
 * Running the program twice on the same component is an EXACT, byte-for-
 * byte inverse -- ALWAYS, for every file it accepts. The transform is a
 * pure involution: complementing the s mantissa bits twice restores the
 * original bits, and nothing else in the stream is touched. There is no
 * generation loss from repeated application, because nothing is ever
 * requantized or decoded to pixels. The endpoint clamping discussed
 * above happens only in a downstream DECODER and does not affect this
 * round-trip: the coefficients we restore are bit-identical regardless.
 * (Verified across 4:4:4 / 4:2:2 / 4:2:0, restart markers, low quality,
 * grayscale, and non-multiple-of-8 dimensions.)
 *
 * It would become lossy ONLY if the operation were changed to do
 * something that requantizes -- it is not lossy as written. The two ways
 * a future edit could break exactness:
 *   - Implementing the TRUE 255 - p negative via the DC -= 8 adjustment
 *     on tables whose DC step does not divide 8: that rounds to the
 *     nearest representable level (~1-level error) AND can change a DC
 *     size category, which breaks the "reuse the same Huffman tables"
 *     property and forces re-encoding.
 *   - Reimplementing as decode-to-pixels -> invert -> re-encode, which
 *     requantizes the entire image.
 * The current 256 - p design avoids both and is exactly reversible.
 *
 * =====================================================================
 * USAGE
 * =====================================================================
 *   jpeginvert in.jpg out.jpg <channels>
 *   channels = comma-separated list of 0|1|2|3 or Y|Cb|Cr|K
 *              (e.g. "Y", "0", "Y,Cr", "0,2,3", "1,2")
 *   Each listed component is negated; components are independent, so
 *   negating any subset is still a perfect involution.
 *
 *   channels = --invert-cmyk
 *   Shorthand for "0,1,2,3" (negate all four components of a CMYK/YCCK
 *   file). PDF-extracted Adobe CMYK JPEGs are commonly stored with a
 *   polarity that makes standalone viewers (Preview, browsers, ImageMagick)
 *   render them inverted; negating all four components fixes that. The
 *   APP14 "Adobe" marker is left untouched on purpose -- see the CMYK note
 *   under EXTENDING THIS.
 *
 * =====================================================================
 * SCOPE / SUPPORTED INPUTS
 * =====================================================================
 * Baseline and extended-sequential Huffman JPEG (SOF0 / SOF1), 8-bit,
 * single scan. Chroma subsampling and restart markers (DRI / RSTn) are
 * handled. Rejected: progressive (SOF2) and arithmetic coding.
 *
 * =====================================================================
 * EXTENDING THIS (notes for future changes)
 * =====================================================================
 * The entire design rests on ONE invariant: for every component, the
 * sequence of Huffman (run, size) symbols and their bit lengths must be
 * preserved, and only mantissa bits may change. Any edit that alters a
 * coefficient's MAGNITUDE (not just its sign) can change a size category
 * or the zero-run structure, which breaks "reuse the same DHT tables"
 * and would require full Huffman re-encoding (and possibly re-optimised
 * tables). Keep transforms sign-only to stay on the lossless fast path.
 *
 *  * CMYK / 4-COMPONENT (Adobe): the 4th component is supported -- the
 *    transcode loop iterates `ncomp` components with [4]-sized
 *    table/component arrays, and the argument parser accepts index 3 / "K"
 *    (and the --invert-cmyk shorthand for all four).
 *    Adobe writes an APP14 marker whose "transform" byte says whether the
 *    data is RGB(0), YCbCr(1) or YCCK(2). PDF-extracted Adobe CMYK/YCCK
 *    JPEGs are routinely stored at a polarity that makes common decoders
 *    (libjpeg/ImageMagick, macOS ImageIO/Preview) render them inverted;
 *    negating all four stored components flips them back -- verified to
 *    render correctly in both ImageMagick and ImageIO on a real YCCK PDF
 *    extraction. We do NOT touch the APP14 marker: on YCCK its transform=2
 *    is what tells the decoder to undo the YCbCr->CMY transform, so dropping
 *    it makes the decoder treat the chroma channels as raw C/M ink and
 *    corrupts colours. (The K name is just a positional alias for index 3,
 *    not a colorimetric guarantee -- which component is "ink" depends on the
 *    transform flag.)
 *
 *  * PROGRESSIVE (SOF2): needs multiple scans, spectral selection
 *    (Ss..Se), successive approximation (Ah/Al) with AC refinement /
 *    correction bits, and EOBn run handling. Negation still preserves
 *    magnitude categories, but the refinement-bit model needs dedicated
 *    handling -- a substantial extension, not a drop-in.
 *
 *  * ARITHMETIC CODING (SOF9 / SOF10): a different entropy coder entirely;
 *    none of the Huffman path here applies.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Whole-file buffer helpers                                          */
/* ------------------------------------------------------------------ */

static uint8_t* g_in; /* input file bytes */
static size_t g_insize;

static void die(const char* msg) {
    fprintf(stderr, "error: %s\n", msg);
    exit(1);
}

static uint8_t* read_file(const char* path, size_t* out) {
    FILE* f = fopen(path, "rb");
    if (!f) die("cannot open input");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 2) die("file too small");
    uint8_t* b = malloc(n);
    if (!b) die("oom");
    if (fread(b, 1, n, f) != (size_t)n) die("short read");
    fclose(f);
    *out = (size_t)n;
    return b;
}

/* ------------------------------------------------------------------ */
/* Huffman decode tables (built from DHT segments)                    */
/* ------------------------------------------------------------------ */

typedef struct {
    int valid;
    uint8_t vals[256];
    int mincode[17], maxcode[17], valptr[17]; /* per code length 1..16 */
} Huff;

static Huff dc_tab[4], ac_tab[4];

static void build_huff(Huff* h, const uint8_t bits[16], const uint8_t* vals, int nvals) {
    memcpy(h->vals, vals, nvals);
    int code = 0, k = 0;
    for (int len = 1; len <= 16; len++) {
        int cnt = bits[len - 1];
        if (cnt == 0) {
            h->maxcode[len] = -1; /* no codes of this length */
        } else {
            h->valptr[len] = k;
            h->mincode[len] = code;
            code += cnt;
            h->maxcode[len] = code - 1;
            k += cnt;
        }
        code <<= 1;
    }
    h->valid = 1;
}

/* ------------------------------------------------------------------ */
/* Bit reader over entropy-coded data (handles FF00 stuffing/markers) */
/* ------------------------------------------------------------------ */

typedef struct {
    const uint8_t* data;
    size_t size;
    size_t pos;      /* next byte to consume */
    uint8_t curbyte; /* bits being shifted out, MSB first */
    int bitcnt;      /* valid bits left in curbyte */
    int marker;      /* nonzero: marker byte we stopped before */
    int eof;
} BitReader;

static int getbit(BitReader* br) {
    if (br->bitcnt == 0) {
        if (br->pos >= br->size) {
            br->eof = 1;
            return 0;
        }
        uint8_t b = br->data[br->pos];
        if (b == 0xFF) {
            uint8_t b2 = (br->pos + 1 < br->size) ? br->data[br->pos + 1] : 0xD9;
            if (b2 == 0x00) {
                br->pos += 2; /* stuffed: literal 0xFF data byte */
            } else {
                br->marker = b2; /* real marker: stop, leave pos at FF */
                br->eof = 1;
                return 0;
            }
        } else {
            br->pos += 1;
        }
        br->curbyte = b;
        br->bitcnt = 8;
    }
    int bit = (br->curbyte >> 7) & 1;
    br->curbyte <<= 1;
    br->bitcnt--;
    return bit;
}

/* Decode one Huffman symbol; also report the matched code (len/value)
 * so we can re-emit it verbatim. */
static int huff_decode(BitReader* br, Huff* h, int* codelen, int* codeval) {
    int code = 0;
    for (int len = 1; len <= 16; len++) {
        code = (code << 1) | getbit(br);
        if (h->maxcode[len] >= 0 && code <= h->maxcode[len]) {
            *codelen = len;
            *codeval = code;
            return h->vals[h->valptr[len] + (code - h->mincode[len])];
        }
    }
    return -1; /* invalid stream */
}

static int read_bits(BitReader* br, int n) {
    int v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | getbit(br);
    return v;
}

/* Consume a restart marker between MCUs (drop pad bits, skip FF Dn). */
static void reader_restart(BitReader* br) {
    br->bitcnt = 0; /* discard 1-bit padding of the partial byte */
    br->eof = 0;
    br->marker = 0;
    if (br->pos + 1 < br->size && br->data[br->pos] == 0xFF &&
        (br->data[br->pos + 1] & 0xF8) == 0xD0) {
        br->pos += 2; /* skip FF D0..D7 */
    }
}

/* ------------------------------------------------------------------ */
/* Bit writer (re-derives FF00 stuffing on output)                    */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t* buf;
    size_t len, cap;
    uint32_t acc;
    int nbits;
} BitWriter;

static void w_raw(BitWriter* w, uint8_t byte) {
    if (w->len + 1 > w->cap) {
        w->cap = w->cap ? w->cap * 2 : 65536;
        w->buf = realloc(w->buf, w->cap);
        if (!w->buf) die("oom");
    }
    w->buf[w->len++] = byte;
}

static void emit_byte(BitWriter* w, uint8_t byte) {
    w_raw(w, byte);
    if (byte == 0xFF) w_raw(w, 0x00); /* byte stuffing */
}

static void put_bit(BitWriter* w, int bit) {
    w->acc = (w->acc << 1) | (bit & 1);
    if (++w->nbits == 8) {
        emit_byte(w, w->acc & 0xFF);
        w->nbits = 0;
        w->acc = 0;
    }
}

static void put_bits(BitWriter* w, int val, int n) {
    for (int i = n - 1; i >= 0; i--) put_bit(w, (val >> i) & 1);
}

static void w_pad_to_byte(BitWriter* w) { /* JPEG pads with 1-bits */
    while (w->nbits) put_bit(w, 1);
}

/* ------------------------------------------------------------------ */
/* Component / scan state                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    int id, h, v;
} Comp;
static Comp comp[4];
static int ncomp;
static int comp_dc[4], comp_ac[4]; /* DC/AC table id per component (SOF order) */

/* Decode one 8x8 block of a component, re-emitting its tokens, flipping
 * mantissa bits iff this is the target component. */
static void transcode_block(BitReader* br, BitWriter* w, int ci, int flip) {
    int cl, cv;
    /* DC */
    int s = huff_decode(br, &dc_tab[comp_dc[ci]], &cl, &cv);
    if (s < 0) die("bad DC code");
    put_bits(w, cv, cl); /* same Huffman symbol */
    if (s > 0) {
        int m = read_bits(br, s);
        if (flip) m = (~m) & ((1 << s) - 1);
        put_bits(w, m, s);
    }
    /* AC, positions 1..63 */
    int k = 1;
    while (k < 64) {
        int rs = huff_decode(br, &ac_tab[comp_ac[ci]], &cl, &cv);
        if (rs < 0) die("bad AC code");
        put_bits(w, cv, cl);
        int r = rs >> 4, sz = rs & 15;
        if (sz == 0) {
            if (r == 15) { /* ZRL: 16 zeros */
                k += 16;
                continue;
            }
            break; /* EOB */
        }
        k += r; /* skip r zeros */
        if (k > 63) die("coefficient overflow");
        int m = read_bits(br, sz);
        if (flip) m = (~m) & ((1 << sz) - 1);
        put_bits(w, m, sz);
        k++;
    }
}

/* ------------------------------------------------------------------ */
/* Marker parsing                                                     */
/* ------------------------------------------------------------------ */

static int rd16(const uint8_t* p) { return (p[0] << 8) | p[1]; }

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr,
                "usage: %s in.jpg out.jpg <channels>\n"
                "  channels = comma-separated 0|1|2|3 or Y|Cb|Cr|K (e.g. \"Y,Cr\" or \"0,2,3\")\n"
                "  channels = --invert-cmyk  shorthand for 0,1,2,3 (a 4-component file)\n",
                argv[0]);
        return 2;
    }

    /* parse channel argument: a comma-separated list, built into a bitmask
     * of component indices. Each listed component will be negated.
     *
     * --invert-cmyk is shorthand for "0,1,2,3": PDF-extracted Adobe CMYK
     * JPEGs are commonly stored with a polarity that makes standalone viewers
     * (Preview, browsers, ImageMagick) render them inverted, and negating all
     * four components fixes that. We deliberately leave the APP14 "Adobe"
     * marker untouched -- on YCCK it carries transform=2, which decoders need
     * to undo the YCbCr->CMY transform; dropping it would corrupt colours.
     * The "mask must fit in ncomp" check below rejects this on non-4-comp files. */
    int target_mask = 0;
    if (!strcmp(argv[3], "--invert-cmyk")) {
        target_mask = 0xF;
    } else {
        char* spec = strdup(argv[3]);
        if (!spec) die("oom");
        for (char* tok = strtok(spec, ","); tok; tok = strtok(NULL, ",")) {
            int idx = 0;
            if (!strcmp(tok, "0") || !strcmp(tok, "Y") || !strcmp(tok, "y")) idx = 0;
            else if (!strcmp(tok, "1") || !strcmp(tok, "Cb") || !strcmp(tok, "cb")) idx = 1;
            else if (!strcmp(tok, "2") || !strcmp(tok, "Cr") || !strcmp(tok, "cr")) idx = 2;
            else if (!strcmp(tok, "3") || !strcmp(tok, "K") || !strcmp(tok, "k")) idx = 3;
            else die("channel must be 0|1|2|3 or Y|Cb|Cr|K");
            target_mask |= 1 << idx;
        }
        free(spec);
        if (target_mask == 0) die("no channels specified");
    }

    g_in = read_file(argv[1], &g_insize);
    if (g_in[0] != 0xFF || g_in[1] != 0xD8) die("not a JPEG (no SOI)");

    int Ri = 0; /* restart interval (MCUs), 0 = none */
    int width = 0, height = 0;
    size_t entropy_start = 0;
    int sof_seen = 0, sos_seen = 0;

    size_t p = 2; /* after SOI */
    while (p + 1 < g_insize && !sos_seen) {
        if (g_in[p] != 0xFF) die("expected marker");
        uint8_t m = g_in[p + 1];

        /* markers with no payload */
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
            p += 2;
            continue;
        }
        if (m == 0xD9) die("EOI before scan");

        if (p + 3 >= g_insize) die("truncated segment");
        int L = rd16(&g_in[p + 2]);        /* length incl. these 2 bytes */
        const uint8_t* seg = &g_in[p + 4]; /* segment body */
        int seglen = L - 2;

        switch (m) {
        case 0xC0:
        case 0xC1: { /* SOF0 / SOF1: baseline-ish */
            int prec = seg[0];
            height = rd16(&seg[1]);
            width = rd16(&seg[3]);
            ncomp = seg[5];
            if (prec != 8) die("only 8-bit precision supported");
            if (ncomp < 1 || ncomp > 4) die("bad component count");
            const uint8_t* cp = &seg[6];
            for (int i = 0; i < ncomp; i++) {
                comp[i].id = cp[0];
                comp[i].h = cp[1] >> 4;
                comp[i].v = cp[1] & 15;
                cp += 3;
            }
            sof_seen = 1;
            break;
        }
        case 0xC2:
            die("progressive JPEG (SOF2) not supported");
        case 0xC3:
        case 0xC5:
        case 0xC6:
        case 0xC7:
        case 0xC9:
        case 0xCA:
        case 0xCB:
            die("unsupported SOF type (arithmetic/lossless/etc.)");

        case 0xC4: { /* DHT: one or more tables */
            int off = 0;
            while (off < seglen) {
                uint8_t tcth = seg[off++];
                int cls = tcth >> 4, id = tcth & 15;
                if (id > 3) die("bad Huffman table id");
                uint8_t bits[16];
                int nv = 0;
                for (int i = 0; i < 16; i++) {
                    bits[i] = seg[off + i];
                    nv += bits[i];
                }
                off += 16;
                const uint8_t* vals = &seg[off];
                off += nv;
                build_huff(cls ? &ac_tab[id] : &dc_tab[id], bits, vals, nv);
            }
            break;
        }
        case 0xDD: /* DRI */
            Ri = rd16(seg);
            break;

        case 0xDA: { /* SOS */
            if (!sof_seen) die("SOS before SOF");
            int ns = seg[0];
            const uint8_t* sp = &seg[1];
            for (int i = 0; i < ns; i++) {
                int cs = sp[0], td = sp[1] >> 4, ta = sp[1] & 15;
                sp += 2;
                int idx = -1;
                for (int j = 0; j < ncomp; j++)
                    if (comp[j].id == cs) idx = j;
                if (idx < 0) die("scan references unknown component");
                comp_dc[idx] = td;
                comp_ac[idx] = ta;
            }
            entropy_start = p + 2 + L; /* first byte of entropy data */
            sos_seen = 1;
            break;
        }
        default: /* APPn, COM, DQT, etc.: skip */
            break;
        }
        p += 2 + L;
    }

    if (!sos_seen) die("no scan found");
    if (target_mask >> ncomp) die("channel index >= number of components");

    /* MCU geometry */
    int hmax = 1, vmax = 1;
    for (int i = 0; i < ncomp; i++) {
        if (comp[i].h > hmax) hmax = comp[i].h;
        if (comp[i].v > vmax) vmax = comp[i].v;
    }
    int mcux = (width + 8 * hmax - 1) / (8 * hmax);
    int mcuy = (height + 8 * vmax - 1) / (8 * vmax);
    long total_mcu = (long)mcux * mcuy;

    /* transcode the scan */
    BitReader br = {0};
    br.data = g_in;
    br.size = g_insize;
    br.pos = entropy_start;
    BitWriter w = {0};

    int rst_idx = 0;
    long since_rst = 0;
    for (long mcu = 0; mcu < total_mcu; mcu++) {
        for (int ci = 0; ci < ncomp; ci++) {
            int flip = (target_mask >> ci) & 1;
            for (int by = 0; by < comp[ci].v; by++)
                for (int bx = 0; bx < comp[ci].h; bx++) transcode_block(&br, &w, ci, flip);
        }
        since_rst++;
        if (Ri && since_rst == Ri && mcu + 1 < total_mcu) {
            /* end this restart interval */
            w_pad_to_byte(&w);
            w_raw(&w, 0xFF);
            w_raw(&w, 0xD0 + (rst_idx & 7));
            rst_idx++;
            since_rst = 0;
            reader_restart(&br);
        }
    }
    w_pad_to_byte(&w);

    /* where did the entropy data end? (start of EOI / next marker) */
    size_t scan_end = br.pos;
    while (scan_end + 1 < g_insize &&
           !(g_in[scan_end] == 0xFF && g_in[scan_end + 1] != 0x00 &&
             !(g_in[scan_end + 1] >= 0xD0 && g_in[scan_end + 1] <= 0xD7))) {
        scan_end++;
    }

    /* assemble output: headers verbatim + new scan + trailer verbatim */
    FILE* out = fopen(argv[2], "wb");
    if (!out) die("cannot open output");
    fwrite(g_in, 1, entropy_start, out);                  /* everything through SOS hdr */
    fwrite(w.buf, 1, w.len, out);                         /* rewritten entropy data */
    fwrite(g_in + scan_end, 1, g_insize - scan_end, out); /* EOI and anything after */
    fclose(out);

    fprintf(stderr, "inverted components");
    for (int i = 0; i < ncomp; i++)
        if ((target_mask >> i) & 1) fprintf(stderr, " %d", i);
    fprintf(stderr, " (%dx%d, %d comps, Ri=%d) -> %s\n", width, height, ncomp, Ri, argv[2]);
    free(w.buf);
    free(g_in);
    return 0;
}
