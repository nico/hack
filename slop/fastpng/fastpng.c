// fastpng: a fast PNG decoder in one C file. See README.md for the ideas behind it.
//
// Layout:
//   1. Portable SIMD: 16-byte vectors through compiler vector extensions and builtins.
//   2. Inflate: a one-shot, libdeflate-style deflate decoder.
//   3. Checksums: CRC-32 and Adler-32, checked only when asked for.
//   4. Unfiltering: PNG's five row filters, several rows at a time where that helps.
//   5. Unpacking: unfiltered rows to 8-bit RGBA or BGRA.
//   6. The PNG container: chunks, the image header, and the decode driver.
//
// Needs clang, or gcc 12 or newer, and a little-endian machine. Vector code is written once and
// compiles to NEON on arm64 and SSE on x86-64; there are no intrinsics.

#include "fastpng.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__clang__) && (!defined(__GNUC__) || __GNUC__ < 12)
#    error "fastpng needs clang, or gcc 12+ (for vector extensions and __builtin_shufflevector)"
#endif
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#    error "fastpng assumes a little-endian machine"
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define FP_INLINE static inline __attribute__((always_inline))
#define FP_NOINLINE static __attribute__((noinline))
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

// ================================================================================================
// 1. Portable SIMD
// ================================================================================================
//
// GCC/clang vector extensions give element-wise arithmetic, comparisons (all-ones or all-zeros
// per lane), and bit casts by plain casts; __builtin_shufflevector gives shuffles with constant
// indices, which lower to tbl/ext/dup on NEON and pshufb/palignr on SSE. Everything vector-shaped
// below is built from these.

typedef u8 u8x16 __attribute__((vector_size(16)));
typedef u32 u32x4 __attribute__((vector_size(16)));
typedef u64 u64x2 __attribute__((vector_size(16)));

#define SHUFFLE(a, b, ...) __builtin_shufflevector((a), (b), __VA_ARGS__)

FP_INLINE u8x16 load16(void const* p)
{
    u8x16 v;
    memcpy(&v, p, sizeof(v));
    return v;
}

FP_INLINE void store16(void* p, u8x16 v)
{
    memcpy(p, &v, sizeof(v));
}

FP_INLINE u8x16 splat8(u8 x)
{
    return (u8x16) { 0 } + x;
}

FP_INLINE u8x16 select8(u8x16 mask, u8x16 if_set, u8x16 if_clear)
{
    return (mask & if_set) | (~mask & if_clear);
}

FP_INLINE u8x16 absolute_difference8(u8x16 x, u8x16 y)
{
    u8x16 const x_is_greater = (u8x16)(x > y);
    return select8(x_is_greater, x, y) - select8(x_is_greater, y, x);
}

// floor((a + b) / 2) without widening.
FP_INLINE u8x16 average_floor8(u8x16 a, u8x16 b)
{
    return (a & b) + ((a ^ b) >> 1);
}

FP_INLINE u64 load_u64(u8 const* p)
{
    u64 v;
    memcpy(&v, p, sizeof(v));
    return v;
}

FP_INLINE u32 load_u32(u8 const* p)
{
    u32 v;
    memcpy(&v, p, sizeof(v));
    return v;
}

FP_INLINE void store_u32(u8* p, u32 v)
{
    memcpy(p, &v, sizeof(v));
}

FP_INLINE u32 read_be32(u8 const* p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

// ================================================================================================
// 2. Inflate
// ================================================================================================
//
// A port of the one-shot decoder in SerenityOS's LibCompress, whose tables and loop are
// libdeflate's design (Eric Biggers, MIT): a decode table entry carries everything a symbol needs
// -- what it is, its value or base, and the bits to drop for its code and its extra bits together
// -- so a literal is one load, one shift and one store, and a match two loads and a few word
// copies; the bit buffer is refilled without a branch; and while both buffers are far from their
// ends the loop runs with no bounds checks at all, a checked loop taking the last few hundred
// bytes. On top of that: two literals per table entry when codes are short (after fpng), and
// matches a few bytes back copied as a repeated vector.
//
// PNG knows how big its inflated data is, so the output is one buffer of that size plus a margin
// the fast loop may scribble into.

// A table entry. Bits not described are zero.
//   Literal:          bit 12 set; bits 23-16 the byte; low byte the code length. In the main
//                     table, when the next code is a literal too and both fit in the table's
//                     bits, also bit 8 set, bits 31-24 the next byte, and the low byte both
//                     codes' length.
//   Length:           bits 24-16 the base; bits 11-8 the code length; low byte code length plus
//                     extra bits, so one shift drops both and the extra bits are what's between.
//   End of block:     bits 15 and 13 set.
//   Subtable pointer: bits 15 and 14 set; bits 30-16 where the subtable starts; bits 11-8 how
//                     many bits index it; low byte the main table's bits.
//   Offset:           bits 31-16 the base; the rest as a length's.
// The low byte is the total to drop, so `bits_left -= entry` works with garbage above it.
#define ENTRY_LITERAL (1u << 12)
#define ENTRY_SECOND_LITERAL (1u << 8)
#define ENTRY_EXCEPTIONAL (1u << 15) // A subtable pointer or the end of the block.
#define ENTRY_SUBTABLE_POINTER (1u << 14)
#define ENTRY_END_OF_BLOCK (1u << 13)

#define PRECODE_TABLE_BITS 7
#define PRECODE_TABLE_SIZE 128 // zlib's `enough 19 7 7`
#define LITLEN_TABLE_BITS 11
#define LITLEN_TABLE_SIZE 2342 // `enough 288 11 15`
#define OFFSET_TABLE_BITS 8
#define OFFSET_TABLE_SIZE 402 // `enough 32 8 15`

#define MAX_CODE_LENGTH 15
#define MAX_PRECODE_LENGTH 7
#define PRECODE_SYMBOL_COUNT 19
#define MAX_LITLEN_SYMBOL_COUNT 288
#define MAX_OFFSET_SYMBOL_COUNT 32
#define MAX_EXTRA_LENGTH_BITS 5
#define MAX_EXTRA_OFFSET_BITS 13
#define MAX_MATCH_LENGTH 258

// The bit buffer holds up to 63 counted bits (one short of the word so the branchless refill
// never shifts by 64), and a refill leaves at least 56 of them.
#define MAX_BITS_LEFT 63
#define BITS_AFTER_REFILL 56
// What the fast loop may write past the output position in one turn (two entries' literals, the
// longest match, and the copy's overrun) and read past the input position (the refills of one
// turn, and the word a refill loads past what it counts).
#define FASTLOOP_BYTES_WRITTEN (2 * 2 + MAX_MATCH_LENGTH + 5 * 8 - 1)
#define FASTLOOP_BYTES_READ ((MAX_BITS_LEFT + 2 * LITLEN_TABLE_BITS + (MAX_CODE_LENGTH + MAX_EXTRA_LENGTH_BITS) + (MAX_CODE_LENGTH + MAX_EXTRA_OFFSET_BITS) + 7) / 8 + 8)

// RFC 1951 3.2.5.
static u16 const length_bases[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static u8 const length_extra_bits[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static u16 const offset_bases[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static u8 const offset_extra_bits[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
// RFC 1951 3.2.7.
static u8 const precode_order[PRECODE_SYMBOL_COUNT] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

typedef enum {
    CODE_PRECODE,
    CODE_LITLEN,
    CODE_OFFSET,
} code_kind;

// The part of a symbol's entry that doesn't depend on its code length.
static u32 symbol_result(code_kind kind, unsigned symbol)
{
    switch (kind) {
    case CODE_PRECODE:
        return symbol << 16;
    case CODE_LITLEN:
        if (symbol < 256)
            return ENTRY_LITERAL | (symbol << 16);
        if (symbol == 256)
            return ENTRY_EXCEPTIONAL | ENTRY_END_OF_BLOCK;
        // 286 and 287 can't occur in a valid stream; they read as the longest match.
        symbol = (symbol > 285 ? 285 : symbol) - 257;
        return ((u32)length_bases[symbol] << 16) | length_extra_bits[symbol];
    case CODE_OFFSET:
        symbol = symbol > 29 ? 29 : symbol;
        return ((u32)offset_bases[symbol] << 16) | offset_extra_bits[symbol];
    }
    return 0;
}

static u32 make_entry(code_kind kind, unsigned symbol, unsigned code_length)
{
    u32 const result = symbol_result(kind, symbol);
    if (result & ENTRY_LITERAL)
        return result + code_length;
    return result + (code_length << 8) + code_length;
}

typedef struct {
    u32 litlen[LITLEN_TABLE_SIZE];
    u32 offset[OFFSET_TABLE_SIZE];
    u32 precode[PRECODE_TABLE_SIZE];
    // The code lengths of a block, with room for a run that overshoots: the longest run is 138
    // zeroes, written whole before the count is checked.
    u8 lengths[MAX_LITLEN_SYMBOL_COUNT + MAX_OFFSET_SYMBOL_COUNT + 137];
    u16 sorted_symbols[MAX_LITLEN_SYMBOL_COUNT];
    unsigned litlen_bits;
    bool literals_paired;
} decode_tables;

// Builds the table a canonical code's symbols are decoded through, indexed by the next
// `table_bits` bits of input (codes are stored first bit lowest). A code longer than that gets a
// subtable, placed after the main table. `table_bits_used`, when given, lets the main table be no
// bigger than the longest code needs, and says how big it came out; `length_counts_used`, when
// given, gets how many symbols have each code length. Returns false when the lengths don't form
// a valid code. After libdeflate's build_decode_table().
static bool build_decode_table(u32* table, u8 const* lengths, unsigned symbol_count, code_kind kind, unsigned table_bits, unsigned longest_allowed, u16* sorted_symbols, unsigned* table_bits_used, unsigned* length_counts_used)
{
    unsigned length_counts[MAX_CODE_LENGTH + 1] = { 0 };
    for (unsigned symbol = 0; symbol < symbol_count; ++symbol)
        length_counts[lengths[symbol]]++;
    if (length_counts_used)
        memcpy(length_counts_used, length_counts, sizeof(length_counts));

    unsigned longest = longest_allowed;
    while (longest > 1 && length_counts[longest] == 0)
        longest--;
    if (table_bits_used) {
        table_bits = table_bits < longest ? table_bits : longest;
        *table_bits_used = table_bits;
    }

    // Symbols sorted by code length, then by value: the order of their codes in a canonical code.
    unsigned offsets[MAX_CODE_LENGTH + 2];
    offsets[0] = 0;
    offsets[1] = length_counts[0];
    u32 codespace_used = 0;
    unsigned length = 1;
    for (; length < longest; ++length) {
        offsets[length + 1] = offsets[length] + length_counts[length];
        codespace_used = (codespace_used << 1) + length_counts[length];
    }
    codespace_used = (codespace_used << 1) + length_counts[length];
    for (unsigned symbol = 0; symbol < symbol_count; ++symbol)
        sorted_symbols[offsets[lengths[symbol]]++] = symbol;
    sorted_symbols += offsets[0]; // Past the unused symbols.

    if (codespace_used > (1u << longest))
        return false; // Over-subscribed.

    if (codespace_used < (1u << longest)) {
        // Incomplete. An empty code is allowed (a block with no matches has no offset code), and
        // so is a code of one symbol with a one-bit code, which zlib allows and reads as '0';
        // both codes map to the symbol so the stream can't index past the table.
        u32 entry;
        if (codespace_used == 0) {
            entry = make_entry(kind, 0, 1);
        } else {
            if (codespace_used != (1u << (longest - 1)) || length_counts[1] != 1)
                return false;
            entry = make_entry(kind, *sorted_symbols, 1);
        }
        for (unsigned i = 0; i < (1u << table_bits); ++i)
            table[i] = entry;
        return true;
    }

    // The codes in order, filling the main table. A code of `length` bits owns every entry whose
    // low bits are it, 2^length apart; rather than stride through, the table is filled as if it
    // had 2^length entries and doubled by copying each time the length grows.
    unsigned codeword = 0;
    length = 1;
    unsigned count;
    while ((count = length_counts[length]) == 0)
        length++;
    unsigned table_end = 1u << length;
    while (length <= table_bits) {
        do {
            table[codeword] = make_entry(kind, *sorted_symbols++, length);
            if (codeword == table_end - 1) {
                // The last code: all ones.
                for (; length < table_bits; ++length) {
                    memcpy(&table[table_end], table, table_end * sizeof(table[0]));
                    table_end <<= 1;
                }
                return true;
            }
            // The next code in canonical order is this one plus one, which with the bits reversed
            // means setting the highest zero bit and clearing the ones above it.
            unsigned const bit = 1u << (31 - __builtin_clz(codeword ^ (table_end - 1)));
            codeword &= bit - 1;
            codeword |= bit;
        } while (--count);

        do {
            if (++length <= table_bits) {
                memcpy(&table[table_end], table, table_end * sizeof(table[0]));
                table_end <<= 1;
            }
        } while ((count = length_counts[length]) == 0);
    }

    // Codes longer than the table, in subtables after it.
    table_end = 1u << table_bits;
    unsigned subtable_prefix = ~0u;
    unsigned subtable_start = 0;
    unsigned subtable_bits = 0;
    for (;;) {
        if ((codeword & ((1u << table_bits) - 1)) != subtable_prefix) {
            subtable_prefix = codeword & ((1u << table_bits) - 1);
            subtable_start = table_end;
            // A subtable for codes of `length` bits needs 2^(length - table_bits) entries, and
            // more if fewer codes than that remain at this length: it grows until the longer
            // codes fill it, which a complete code always does.
            subtable_bits = length - table_bits;
            codespace_used = count;
            while (codespace_used < (1u << subtable_bits)) {
                subtable_bits++;
                codespace_used = (codespace_used << 1) + length_counts[table_bits + subtable_bits];
            }
            table_end = subtable_start + (1u << subtable_bits);
            table[subtable_prefix] = ((u32)subtable_start << 16) | ENTRY_EXCEPTIONAL | ENTRY_SUBTABLE_POINTER | (subtable_bits << 8) | table_bits;
        }

        u32 const entry = make_entry(kind, *sorted_symbols++, length - table_bits);
        unsigned const stride = 1u << (length - table_bits);
        for (unsigned i = subtable_start + (codeword >> table_bits); i < table_end; i += stride)
            table[i] = entry;

        if (codeword == (1u << length) - 1)
            return true;
        unsigned const bit = 1u << (31 - __builtin_clz(codeword ^ ((1u << length) - 1)));
        codeword &= bit - 1;
        codeword |= bit;
        count--;
        while (count == 0)
            count = length_counts[++length];
    }
}

// Gives each literal in a literal/length main table the literal after it, where both codes fit
// in the table's bits: then the entry's index holds both codes, and the second one's entry is at
// the index shifted past the first. Downwards, so that entry is still a single literal.
// `length_counts` is how many literals have each code length. Pairing costs about as much as
// building the table, and a block can be a few thousand symbols, so it's done only when a
// quarter of the entries or more would pair, as they do when literal codes are short (smooth
// images, after filtering); returns whether it was.
static bool pair_literals(u32* table, unsigned table_bits, unsigned const* length_counts)
{
    // Exactly how many entries would pair: a literal code of length a heads 2^(table_bits - a)
    // of them, and a second of length b fits in 2^(table_bits - a - b) of those.
    unsigned paired = 0;
    for (unsigned a = 1; a < table_bits; ++a)
        for (unsigned b = 1; a + b <= table_bits; ++b)
            paired += (length_counts[a] * length_counts[b]) << (table_bits - a - b);
    if (paired < (1u << table_bits) / 4)
        return false;

    for (unsigned i = 1u << table_bits; i-- > 0;) {
        u32 const first = table[i];
        unsigned const first_length = (u8)first;
        u32 const second = table[i >> first_length];
        bool const fits = (first & second & ENTRY_LITERAL) && first_length + (u8)second <= table_bits;
        table[i] = fits ? first + ((second & 0xff0000) << 8) + ENTRY_SECOND_LITERAL + (u8)second : first;
    }
    return true;
}

static void build_fixed_tables(decode_tables* t)
{
    // RFC 1951 3.2.6. (Seven- to nine-bit literal codes never pair in a nine-bit table.)
    unsigned i = 0;
    for (; i < 144; ++i)
        t->lengths[i] = 8;
    for (; i < 256; ++i)
        t->lengths[i] = 9;
    for (; i < 280; ++i)
        t->lengths[i] = 7;
    for (; i < 288; ++i)
        t->lengths[i] = 8;
    for (; i < 288 + 32; ++i)
        t->lengths[i] = 5;
    build_decode_table(t->offset, t->lengths + 288, 32, CODE_OFFSET, OFFSET_TABLE_BITS, MAX_CODE_LENGTH, t->sorted_symbols, NULL, NULL);
    build_decode_table(t->litlen, t->lengths, 288, CODE_LITLEN, LITLEN_TABLE_BITS, MAX_CODE_LENGTH, t->sorted_symbols, &t->litlen_bits, NULL);
    t->literals_paired = false;
}

FP_INLINE u64 bit_mask(unsigned count)
{
    return ((u64)1 << count) - 1;
}

FP_INLINE void copy_word(u8 const* src, u8* dst)
{
    memcpy(dst, src, 8);
}

// The first `offset` (2 to 7) bytes of `bytes` repeated, and how many bytes of it are whole repeats.
FP_INLINE u8x16 repeated_pattern(u8x16 bytes, unsigned offset, unsigned* stride)
{
    switch (offset) {
    case 2:
        *stride = 16;
        return SHUFFLE(bytes, bytes, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1);
    case 3:
        *stride = 15;
        return SHUFFLE(bytes, bytes, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0);
    case 4:
        *stride = 16;
        return SHUFFLE(bytes, bytes, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3);
    case 5:
        *stride = 15;
        return SHUFFLE(bytes, bytes, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0);
    case 6:
        *stride = 12;
        return SHUFFLE(bytes, bytes, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3);
    default:
        *stride = 14;
        return SHUFFLE(bytes, bytes, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1);
    }
}

// The decoder's position, handed to and from the fast loop, which keeps it in locals: state that
// lives in memory during the loop costs it a factor of two or three.
typedef struct {
    u8 const* in_next;
    u64 bitbuf;
    u32 bits_left; // Only the low byte counts; see the refills.
    u8* out_next;
} inflate_state;

// Loads a whole word and counts the bytes that fit, without a branch.
#define REFILL_UNCHECKED()                                \
    do {                                                  \
        bitbuf |= load_u64(in_next) << (u8)bits_left;     \
        in_next += 7;                                     \
        in_next -= (bits_left >> 3) & 7;                  \
        bits_left |= BITS_AFTER_REFILL;                   \
    } while (0)

// The loop for inside the margins, for a table with paired literals or one without (`paired` is
// a constant at each call): without, a literal is one byte and its store needn't wait for the
// entry's count. Returns 1 at the end of the block, 0 when it reached a margin, or an error.
FP_INLINE int inflate_fastloop(inflate_state* state, u32 const* litlen_table, u32 const* offset_table, u64 litlen_mask, u8 const* in_fastloop_end, u8 const* out_fastloop_end, u8 const* out, bool const paired)
{
    u8 const* in_next = state->in_next;
    u64 bitbuf = state->bitbuf;
    u32 bits_left = state->bits_left;
    u8* out_next = state->out_next;
    u64 saved_bitbuf;
    int result = 0;

    // A literal entry's one or two bytes, both stored, the second overwritten next when unused.
#define WRITE_LITERALS(literals)                                                 \
    do {                                                                         \
        if (paired) {                                                            \
            u16 const bytes_ = (u16)((literals) >> 16);                          \
            memcpy(out_next, &bytes_, 2);                                        \
            out_next += 1 + (((literals) & ENTRY_SECOND_LITERAL) ? 1 : 0);       \
        } else {                                                                 \
            *out_next++ = (u8)((literals) >> 16);                                \
        }                                                                        \
    } while (0)

    // The next entry is loaded before each turn so its latency overlaps the turn's work. Each
    // refill leaves at least 56 counted bits plus a few that can be looked at but not consumed,
    // which the turn budgets so the loads come before the refills they'd otherwise wait on.
    REFILL_UNCHECKED();
    u32 entry = litlen_table[bitbuf & litlen_mask];
    do {
        saved_bitbuf = bitbuf;
        bitbuf >>= (u8)entry;
        bits_left -= entry;

        if (entry & ENTRY_LITERAL) {
            // A literal or two, and up to two more entries' after it: 56 bits cover three
            // table-sized codes and a whole length, or two of them, a longest code and the next
            // table load.
            u32 literals = entry;
            entry = litlen_table[bitbuf & litlen_mask];
            saved_bitbuf = bitbuf;
            bitbuf >>= (u8)entry;
            bits_left -= entry;
            WRITE_LITERALS(literals);
            if (entry & ENTRY_LITERAL) {
                literals = entry;
                entry = litlen_table[bitbuf & litlen_mask];
                saved_bitbuf = bitbuf;
                bitbuf >>= (u8)entry;
                bits_left -= entry;
                WRITE_LITERALS(literals);
                if (entry & ENTRY_LITERAL) {
                    literals = entry;
                    entry = litlen_table[bitbuf & litlen_mask];
                    REFILL_UNCHECKED();
                    WRITE_LITERALS(literals);
                    continue;
                }
            }
            // What's in `entry` now is consumed and not a literal.
        }

        if (UNLIKELY(entry & ENTRY_EXCEPTIONAL)) {
            if (UNLIKELY(entry & ENTRY_END_OF_BLOCK)) {
                result = 1;
                break;
            }
            // A subtable: its entry can be anything the main table's can.
            entry = litlen_table[(entry >> 16) + (bitbuf & bit_mask((entry >> 8) & 0x3F))];
            saved_bitbuf = bitbuf;
            bitbuf >>= (u8)entry;
            bits_left -= entry;
            if (entry & ENTRY_LITERAL) {
                u32 const literal = entry >> 16;
                entry = litlen_table[bitbuf & litlen_mask];
                REFILL_UNCHECKED();
                *out_next++ = (u8)literal;
                continue;
            }
            if (UNLIKELY(entry & ENTRY_END_OF_BLOCK)) {
                result = 1;
                break;
            }
        }

        // A match: the base from the entry, the extra bits from the bits its code was dropped
        // from, which the drop already covered.
        u32 length = entry >> 16;
        length += (saved_bitbuf & bit_mask((u8)entry)) >> (u8)(entry >> 8);

        entry = offset_table[bitbuf & bit_mask(OFFSET_TABLE_BITS)];
        if (UNLIKELY(entry & ENTRY_EXCEPTIONAL)) {
            if (UNLIKELY((u8)bits_left < (MAX_CODE_LENGTH + MAX_EXTRA_OFFSET_BITS) + LITLEN_TABLE_BITS - 1))
                REFILL_UNCHECKED();
            bitbuf >>= OFFSET_TABLE_BITS;
            bits_left -= OFFSET_TABLE_BITS;
            entry = offset_table[(entry >> 16) + (bitbuf & bit_mask((entry >> 8) & 0x3F))];
        } else if (UNLIKELY((u8)bits_left < (OFFSET_TABLE_BITS + MAX_EXTRA_OFFSET_BITS) + LITLEN_TABLE_BITS - 1)) {
            REFILL_UNCHECKED();
        }
        saved_bitbuf = bitbuf;
        bitbuf >>= (u8)entry;
        bits_left -= entry;
        u32 offset = entry >> 16;
        offset += (saved_bitbuf & bit_mask((u8)entry)) >> (u8)(entry >> 8);

        if (UNLIKELY(offset > (size_t)(out_next - out))) {
            result = FASTPNG_ERROR_BAD_ZLIB;
            break;
        }
        u8 const* src = out_next - offset;
        u8* dst = out_next;
        out_next += length;

        // The next turn's entry and bits, before the copy, so the copy's latency overlaps theirs.
        entry = litlen_table[bitbuf & litlen_mask];
        REFILL_UNCHECKED();

        // The copy, a word at a time and past the match's end into the margin, since most matches
        // are a few words. A source a word or more back is final as it's read; an offset of one
        // is a byte repeated. Anything between is its first `offset` bytes repeated: a short
        // match advances by the offset, each store committing that many more bytes, and a longer
        // one builds the repeats in a vector once and stores as many whole ones at a time as fit
        // (so no load waits for the store before it).
        if (offset >= 8) {
            copy_word(src, dst);
            copy_word(src + 8, dst + 8);
            copy_word(src + 16, dst + 16);
            copy_word(src + 24, dst + 24);
            copy_word(src + 32, dst + 32);
            src += 40;
            dst += 40;
            while (dst < out_next) {
                copy_word(src, dst);
                copy_word(src + 8, dst + 8);
                copy_word(src + 16, dst + 16);
                copy_word(src + 24, dst + 24);
                copy_word(src + 32, dst + 32);
                src += 40;
                dst += 40;
            }
        } else if (offset == 1) {
            u64 const v = 0x0101010101010101ull * src[0];
            memcpy(dst, &v, 8);
            memcpy(dst + 8, &v, 8);
            memcpy(dst + 16, &v, 8);
            memcpy(dst + 24, &v, 8);
            dst += 32;
            while (dst < out_next) {
                memcpy(dst, &v, 8);
                memcpy(dst + 8, &v, 8);
                memcpy(dst + 16, &v, 8);
                memcpy(dst + 24, &v, 8);
                dst += 32;
            }
        } else if (length < 16) {
            copy_word(src, dst);
            src += offset;
            dst += offset;
            copy_word(src, dst);
            src += offset;
            dst += offset;
            do {
                copy_word(src, dst);
                src += offset;
                dst += offset;
                copy_word(src, dst);
                src += offset;
                dst += offset;
            } while (dst < out_next);
        } else {
            unsigned stride;
            u8x16 const pattern = repeated_pattern((u8x16)(u64x2) { load_u64(src), 0 }, offset, &stride);
            store16(dst, pattern);
            dst += stride;
            while (dst < out_next) {
                store16(dst, pattern);
                dst += stride;
            }
        }
    } while (in_next < in_fastloop_end && out_next < out_fastloop_end);
#undef WRITE_LITERALS

    state->in_next = in_next;
    state->bitbuf = bitbuf;
    state->bits_left = bits_left;
    state->out_next = out_next;
    return result;
}

// Inflates the raw deflate stream `in` into `out`, which has room for `out_capacity` bytes, and
// says how many it wrote and how many input bytes the stream was. The stream may end before the
// input does.
FP_NOINLINE int inflate_stream(u8 const* in, size_t in_size, u8* out, size_t out_capacity, size_t* out_size, size_t* in_used)
{
    // On the stack and not cleared: the tables are written before they're read.
    decode_tables tables;
    decode_tables fixed_tables;
    bool fixed_tables_built = false;

    u8 const* const in_end = in + in_size;
    u8 const* const in_fastloop_end = in_end - (in_size < FASTLOOP_BYTES_READ ? in_size : FASTLOOP_BYTES_READ);
    u8 const* in_next = in;
    u64 bitbuf = 0;
    u64 saved_bitbuf = 0;
    u32 bits_left = 0;
    size_t overread_count = 0; // Zero bytes read past the input, which a refill near its end does.

    u8* const out_end = out + out_capacity;
    u8* const out_fastloop_end = out_end - (out_capacity < FASTLOOP_BYTES_WRITTEN ? out_capacity : FASTLOOP_BYTES_WRITTEN);
    u8* out_next = out;

    // Near the end of the input: a word when there's one, else bytes, zeroes past the end.
#define REFILL()                                                                \
    do {                                                                        \
        if (LIKELY(in_end - in_next >= 8)) {                                    \
            REFILL_UNCHECKED();                                                 \
        } else {                                                                \
            while ((u8)bits_left < BITS_AFTER_REFILL) {                         \
                if (LIKELY(in_next != in_end)) {                                \
                    bitbuf |= (u64)*in_next++ << (u8)bits_left;                 \
                } else if (++overread_count > 8) {                              \
                    return FASTPNG_ERROR_TRUNCATED;                             \
                }                                                               \
                bits_left += 8;                                                 \
            }                                                                   \
        }                                                                       \
    } while (0)
#define CONSUME(count)          \
    do {                        \
        bitbuf >>= (count);     \
        bits_left -= (count);   \
    } while (0)

    bool final_block = false;
    while (!final_block) {
        REFILL();
        final_block = bitbuf & 1;
        unsigned const block_type = (bitbuf >> 1) & 3;

        if (block_type == 0) {
            // Stored: the input continues at the next byte, which the refills have read ahead of.
            CONSUME(3);
            bits_left = (u8)bits_left;
            if (overread_count > (bits_left >> 3))
                return FASTPNG_ERROR_TRUNCATED;
            in_next -= (bits_left >> 3) - overread_count;
            overread_count = 0;
            bitbuf = 0;
            bits_left = 0;
            if (in_end - in_next < 4)
                return FASTPNG_ERROR_TRUNCATED;
            u16 const length = in_next[0] | (in_next[1] << 8);
            u16 const negated_length = in_next[2] | (in_next[3] << 8);
            in_next += 4;
            if ((length ^ 0xffff) != negated_length)
                return FASTPNG_ERROR_BAD_ZLIB;
            if (length > in_end - in_next)
                return FASTPNG_ERROR_TRUNCATED;
            if (length > out_end - out_next)
                return FASTPNG_ERROR_BAD_ZLIB;
            memcpy(out_next, in_next, length);
            in_next += length;
            out_next += length;
            continue;
        }

        decode_tables const* t;
        if (block_type == 1) {
            CONSUME(3);
            if (!fixed_tables_built) {
                build_fixed_tables(&fixed_tables);
                fixed_tables_built = true;
            }
            t = &fixed_tables;
        } else if (block_type == 2) {
            // RFC 1951 3.2.7: the counts, the code-length code's lengths, and through that code
            // the lengths of the other two.
            unsigned const litlen_count = 257 + ((bitbuf >> 3) & bit_mask(5));
            unsigned const offset_count = 1 + ((bitbuf >> 8) & bit_mask(5));
            unsigned const precode_count = 4 + ((bitbuf >> 13) & bit_mask(4));
            CONSUME(17);

            u8 precode_lengths[PRECODE_SYMBOL_COUNT] = { 0 };
            for (unsigned i = 0; i < precode_count; ++i) {
                if ((u8)bits_left < 3)
                    REFILL();
                precode_lengths[precode_order[i]] = bitbuf & bit_mask(3);
                CONSUME(3);
            }
            if (!build_decode_table(tables.precode, precode_lengths, PRECODE_SYMBOL_COUNT, CODE_PRECODE, PRECODE_TABLE_BITS, MAX_PRECODE_LENGTH, tables.sorted_symbols, NULL, NULL))
                return FASTPNG_ERROR_BAD_ZLIB;

            u8* const lengths = tables.lengths;
            unsigned const wanted = litlen_count + offset_count;
            unsigned i = 0;
            do {
                if ((u8)bits_left < MAX_PRECODE_LENGTH + 7)
                    REFILL();
                u32 const entry = tables.precode[bitbuf & bit_mask(PRECODE_TABLE_BITS)];
                bitbuf >>= (u8)entry;
                bits_left -= entry;
                unsigned const symbol = entry >> 16;
                if (symbol < 16) {
                    lengths[i++] = symbol;
                    continue;
                }
                // A run. The short ones write their longest run whole, which the array's slack
                // allows, and the count is checked once at the end.
                if (symbol == 16) {
                    if (i == 0)
                        return FASTPNG_ERROR_BAD_ZLIB;
                    unsigned const count = 3 + (bitbuf & bit_mask(2));
                    CONSUME(2);
                    u8 const previous = lengths[i - 1];
                    for (unsigned j = 0; j < 6; ++j)
                        lengths[i + j] = previous;
                    i += count;
                } else if (symbol == 17) {
                    unsigned const count = 3 + (bitbuf & bit_mask(3));
                    CONSUME(3);
                    for (unsigned j = 0; j < 10; ++j)
                        lengths[i + j] = 0;
                    i += count;
                } else {
                    unsigned const count = 11 + (bitbuf & bit_mask(7));
                    CONSUME(7);
                    memset(&lengths[i], 0, count);
                    i += count;
                }
            } while (i < wanted);
            if (i != wanted)
                return FASTPNG_ERROR_BAD_ZLIB;

            if (!build_decode_table(tables.offset, lengths + litlen_count, offset_count, CODE_OFFSET, OFFSET_TABLE_BITS, MAX_CODE_LENGTH, tables.sorted_symbols, NULL, NULL))
                return FASTPNG_ERROR_BAD_ZLIB;
            unsigned length_counts[MAX_CODE_LENGTH + 1];
            if (!build_decode_table(tables.litlen, lengths, litlen_count, CODE_LITLEN, LITLEN_TABLE_BITS, MAX_CODE_LENGTH, tables.sorted_symbols, &tables.litlen_bits, length_counts))
                return FASTPNG_ERROR_BAD_ZLIB;
            for (unsigned symbol = 256; symbol < litlen_count; ++symbol)
                length_counts[lengths[symbol]]--;
            // Pairing visits every entry, which a block in the last few input bytes can't repay.
            tables.literals_paired = (size_t)(in_end - in_next) >= 2 * LITLEN_TABLE_SIZE && pair_literals(tables.litlen, tables.litlen_bits, length_counts);
            t = &tables;
        } else {
            return FASTPNG_ERROR_BAD_ZLIB;
        }

        u32 const* const litlen_table = t->litlen;
        u32 const* const offset_table = t->offset;
        u64 const litlen_mask = bit_mask(t->litlen_bits);

        // The symbols of the block.
        for (;;) {
            if (in_next < in_fastloop_end && out_next < out_fastloop_end) {
                inflate_state state = { in_next, bitbuf, bits_left, out_next };
                int const result = t->literals_paired
                    ? inflate_fastloop(&state, litlen_table, offset_table, litlen_mask, in_fastloop_end, out_fastloop_end, out, true)
                    : inflate_fastloop(&state, litlen_table, offset_table, litlen_mask, in_fastloop_end, out_fastloop_end, out, false);
                in_next = state.in_next;
                bitbuf = state.bitbuf;
                bits_left = state.bits_left;
                out_next = state.out_next;
                if (result < 0)
                    return result;
                if (result == 1)
                    break;
            }

            // One symbol, with everything checked: near the end of the input or the output.
            REFILL();
            u32 entry = litlen_table[bitbuf & litlen_mask];
            saved_bitbuf = bitbuf;
            bitbuf >>= (u8)entry;
            bits_left -= entry;
            if (UNLIKELY(entry & ENTRY_SUBTABLE_POINTER)) {
                entry = litlen_table[(entry >> 16) + (bitbuf & bit_mask((entry >> 8) & 0x3F))];
                saved_bitbuf = bitbuf;
                bitbuf >>= (u8)entry;
                bits_left -= entry;
            }
            u32 length = entry >> 16;
            if (entry & ENTRY_LITERAL) {
                size_t const count = (entry & ENTRY_SECOND_LITERAL) ? 2 : 1;
                if (UNLIKELY((size_t)(out_end - out_next) < count))
                    return FASTPNG_ERROR_BAD_ZLIB;
                *out_next++ = (u8)length;
                if (count == 2)
                    *out_next++ = (u8)(length >> 8);
                continue;
            }
            if (UNLIKELY(entry & ENTRY_END_OF_BLOCK))
                break;
            length += (saved_bitbuf & bit_mask((u8)entry)) >> (u8)(entry >> 8);
            if (length > (size_t)(out_end - out_next))
                return FASTPNG_ERROR_BAD_ZLIB;

            entry = offset_table[bitbuf & bit_mask(OFFSET_TABLE_BITS)];
            if (UNLIKELY(entry & ENTRY_EXCEPTIONAL)) {
                CONSUME(OFFSET_TABLE_BITS);
                entry = offset_table[(entry >> 16) + (bitbuf & bit_mask((entry >> 8) & 0x3F))];
            }
            u32 offset = entry >> 16;
            offset += (bitbuf & bit_mask((u8)entry)) >> (u8)(entry >> 8);
            bitbuf >>= (u8)entry;
            bits_left -= entry;

            if (offset > (size_t)(out_next - out))
                return FASTPNG_ERROR_BAD_ZLIB;
            u8 const* src = out_next - offset;
            u8* dst = out_next;
            out_next += length;
            do {
                *dst++ = *src++;
            } while (dst < out_next);
        }
    }
#undef REFILL
#undef CONSUME

    // What the refills read ahead and didn't consume isn't part of the stream.
    bits_left = (u8)bits_left;
    size_t const unread = (bits_left >> 3) > overread_count ? (bits_left >> 3) - overread_count : 0;
    *in_used = (in_next - in) - unread;
    *out_size = out_next - out;
    return FASTPNG_OK;
}

// ================================================================================================
// 3. Checksums
// ================================================================================================
//
// Every chunk ends with a CRC-32 of its type and data, and the zlib stream with an Adler-32 of the
// inflated data. Neither is checked unless asked for (FASTPNG_CHECK_CRC, FASTPNG_CHECK_ADLER32);
// unasked, none of this runs. When asked: CRCs with the CPU's CRC32 instructions where there are
// some (8 bytes per instruction), else eight table lookups per 8 bytes; Adler-32 sixteen bytes at
// a time in vectors, over each batch of rows just before it's unfiltered, while it's in cache.

// CRC-32 as in PNG and zlib: the reflected polynomial 0xedb88320, starting from and finishing
// with all ones.
#if defined(__ARM_FEATURE_CRC32)
// clang and gcc name the CRC32 instruction builtins differently.
#    if defined(__clang__)
#        define CRC32_8_BYTES __builtin_arm_crc32d
#        define CRC32_1_BYTE __builtin_arm_crc32b
#    else
#        define CRC32_8_BYTES __builtin_aarch64_crc32x
#        define CRC32_1_BYTE __builtin_aarch64_crc32b
#    endif

typedef struct {
    int unused;
} crc32_tables;

static void crc32_init(crc32_tables* tables)
{
    (void)tables;
}

static u32 crc32_update(crc32_tables const* tables, u32 crc, u8 const* p, size_t size)
{
    (void)tables;
    for (; size >= 8; size -= 8, p += 8)
        crc = CRC32_8_BYTES(crc, load_u64(p));
    for (; size > 0; --size)
        crc = CRC32_1_BYTE(crc, *p++);
    return crc;
}
#else
// Slice-by-8, its tables built per decode (a few microseconds) rather than kept in a global.
typedef struct {
    u32 t[8][256];
} crc32_tables;

static void crc32_init(crc32_tables* tables)
{
    for (u32 i = 0; i < 256; ++i) {
        u32 c = i;
        for (int k = 0; k < 8; ++k)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
        tables->t[0][i] = c;
    }
    for (int slice = 1; slice < 8; ++slice) {
        for (u32 i = 0; i < 256; ++i)
            tables->t[slice][i] = (tables->t[slice - 1][i] >> 8) ^ tables->t[0][tables->t[slice - 1][i] & 0xff];
    }
}

static u32 crc32_update(crc32_tables const* tables, u32 crc, u8 const* p, size_t size)
{
    u32 const(*t)[256] = tables->t;
    for (; size >= 8; size -= 8, p += 8) {
        u64 const v = load_u64(p) ^ crc;
        crc = t[7][v & 0xff] ^ t[6][(v >> 8) & 0xff] ^ t[5][(v >> 16) & 0xff] ^ t[4][(v >> 24) & 0xff]
            ^ t[3][(v >> 32) & 0xff] ^ t[2][(v >> 40) & 0xff] ^ t[1][(v >> 48) & 0xff] ^ t[0][v >> 56];
    }
    for (; size > 0; --size)
        crc = (crc >> 8) ^ t[0][(crc ^ *p++) & 0xff];
    return crc;
}
#endif

// Adler-32 (RFC 1950): s1 is 1 plus the sum of the bytes, s2 the sum of s1 after each byte, both
// modulo 65521. Sixteen bytes add sixteen times s1 (as of before them) to s2, plus each byte
// weighted by how many of the sixteen it's before the end of. In vectors, each sixteen bytes add
// into four lanes of byte sums (pairwise) and into sixteen 16-bit column sums, and the running
// byte sums before each sixteen add up into four more lanes; the weights apply to the column sums
// once per block, since the weighted sum of every sixteen is the weighted sum of the columns.
// 256 sixteens keep the columns within 16 bits.
#define ADLER32_BASE 65521
#define ADLER32_BLOCK 4096

typedef u16 u16x8 __attribute__((vector_size(16)));
typedef u32 u32x8 __attribute__((vector_size(32)));

typedef struct {
    u32 s1;
    u32 s2;
} adler32_state;

// The sixteen bytes, added pairwise into eight u16s, then into four u32s.
FP_INLINE u32x4 sum_bytes_into_four(u8x16 v)
{
    u16x8 const pairs = __builtin_convertvector(SHUFFLE(v, v, 0, 2, 4, 6, 8, 10, 12, 14), u16x8) + __builtin_convertvector(SHUFFLE(v, v, 1, 3, 5, 7, 9, 11, 13, 15), u16x8);
    return __builtin_convertvector(SHUFFLE(pairs, pairs, 0, 2, 4, 6), u32x4) + __builtin_convertvector(SHUFFLE(pairs, pairs, 1, 3, 5, 7), u32x4);
}

FP_INLINE u64 sum_lanes(u32x4 v)
{
    return (u64)v[0] + v[1] + v[2] + v[3];
}

FP_INLINE u64 weighted_sum(u16x8 columns, u32x8 weights)
{
    u32x8 const products = __builtin_convertvector(columns, u32x8) * weights;
    return sum_lanes(SHUFFLE(products, products, 0, 1, 2, 3) + SHUFFLE(products, products, 4, 5, 6, 7));
}

// Out of line: inlined into the row loop, it slowed the unfiltering around it by half.
FP_NOINLINE void adler32_update(adler32_state* state, u8 const* p, size_t size)
{
    u32x8 const first_weights = { 16, 15, 14, 13, 12, 11, 10, 9 };
    u32x8 const second_weights = { 8, 7, 6, 5, 4, 3, 2, 1 };
    u32 s1 = state->s1;
    u32 s2 = state->s2;
    while (size >= 16) {
        size_t const block = size < ADLER32_BLOCK ? size - size % 16 : ADLER32_BLOCK;
        u32x4 bytes = { 0 }, sums_before = { 0 };
        u16x8 first_columns = { 0 }, second_columns = { 0 };
        for (size_t i = 0; i < block; i += 16) {
            u8x16 const v = load16(p + i);
            sums_before += bytes;
            bytes += sum_bytes_into_four(v);
            first_columns += __builtin_convertvector(SHUFFLE(v, v, 0, 1, 2, 3, 4, 5, 6, 7), u16x8);
            second_columns += __builtin_convertvector(SHUFFLE(v, v, 8, 9, 10, 11, 12, 13, 14, 15), u16x8);
        }
        u64 const weighted = weighted_sum(first_columns, first_weights) + weighted_sum(second_columns, second_weights);
        s2 = (u32)((s2 + (u64)s1 * block + 16 * sum_lanes(sums_before) + weighted) % ADLER32_BASE);
        s1 = (u32)((s1 + sum_lanes(bytes)) % ADLER32_BASE);
        p += block;
        size -= block;
    }
    for (; size > 0; --size) {
        s1 += *p++;
        s2 += s1;
    }
    state->s1 = s1 % ADLER32_BASE;
    state->s2 = s2 % ADLER32_BASE;
}

// ================================================================================================
// 4. Unfiltering
// ================================================================================================
//
// https://www.w3.org/TR/png-3/#9Filter-types. Each row starts with a filter type, and its bytes
// are differences from a prediction made from the byte one pixel to the left (a), the byte above
// (b), and the byte above-left (c). Undoing None and Up is trivially parallel. Sub, Average and
// Paeth make each pixel wait for the one to its left -- a chain of dependent operations per
// channel across the row -- and the tricks below are all about shortening or interleaving those
// chains:
//   - Sub is a prefix sum, done as log-step shifted adds in a vector, so the chain is one add per
//     16 bytes instead of one per pixel.
//   - Average and Paeth keep the left pixel in registers (not re-read from memory), with the
//     per-channel loop unrolled for each pixel size.
//   - Several rows run at once on a diagonal: a pixel only needs the row above up to one pixel to
//     its right, so row r can work on pixel x - r while row 0 works on pixel x. Four rows of RGB(A)
//     fill one vector; Paeth, whose chain is long, does eight rows in two vectors in about the
//     time of four. Small pixels do Average two rows at once.
// Unfiltering happens in place in the inflated data, 16 rows at a time, each batch unpacked to the
// output right after, while it's still in cache.

typedef enum {
    FILTER_NONE = 0,
    FILTER_SUB = 1,
    FILTER_UP = 2,
    FILTER_AVERAGE = 3,
    FILTER_PAETH = 4,
} filter_type;

// The spec's predictor is
//     p = a + b - c, pa = |p - a|, pb = |p - b|, pc = |p - c|
//     pa <= pb && pa <= pc ? a : pb <= pc ? b : c
// This equivalent one is stb_image's (it gives the same result for all 2^24 inputs). When
// unfiltering, `a` is the byte just written, and here only one subtraction, a min and max, and
// two compare-and-selects wait for it, where the spec's has three absolute differences.
FP_INLINE u8 paeth_predictor(u8 a, u8 b, u8 c)
{
    int const threshold = c * 3 - (a + b);
    u8 const low = a < b ? a : b;
    u8 const high = a < b ? b : a;
    u8 const low_or_c = high <= threshold ? low : c;
    return threshold <= low ? high : low_or_c;
}

// Sixteen bytes at once, with every quantity in 8 bits (after FPNGE's formulation; same result for
// all 2^24 inputs): pa = |b - c| and pb = |a - c|. pc = |(a - c) + (b - c)| is |pa - pb| when a - c
// and b - c have opposite signs, and else pa + pb, which may not fit in 8 bits -- but then it's at
// least pa and pb, so 255 serves as well.
FP_INLINE u8x16 paeth_predictor16(u8x16 a, u8x16 b, u8x16 c)
{
    u8x16 const pa = absolute_difference8(b, c);
    u8x16 const pb = absolute_difference8(a, c);
    u8x16 const opposite_signs = (u8x16)((a < c) ^ (b < c));
    u8x16 sum = pa + pb;
    sum |= (u8x16)(sum < pa);
    u8x16 const pc = select8(opposite_signs, absolute_difference8(pa, pb), sum);
    u8x16 const use_a = (u8x16)((pa <= pb) & (pa <= pc));
    u8x16 const use_b = (u8x16)(pb <= pc);
    return select8(use_a, a, select8(use_b, b, c));
}

// Sub as a prefix sum. A vector of pixels adds itself shifted by one pixel, then by two, then by
// four, which don't depend on earlier vectors; only adding the previous vector's last pixel does,
// so the chain is one add per vector. Bytes after the vector's last whole pixel get zeros added,
// and are stored unchanged. When pixels don't fill a vector, those bytes are the next vector's
// first ones, so the next vector is loaded before this one is stored: a load that overlaps an
// earlier store part way waits for it to reach the cache.
// Shuffles need constant indices, so this is a macro, instantiated per pixel size.
#define SHIFT_INDEX(i, shift, used) (((i) >= (shift) && (i) < (used)) ? (i) - (shift) : 16)
#define SHIFT_BYTES_UP(v, s, u) SHUFFLE(v, (u8x16) { 0 }, SHIFT_INDEX(0, s, u), SHIFT_INDEX(1, s, u), SHIFT_INDEX(2, s, u), SHIFT_INDEX(3, s, u), SHIFT_INDEX(4, s, u), SHIFT_INDEX(5, s, u), SHIFT_INDEX(6, s, u), SHIFT_INDEX(7, s, u), SHIFT_INDEX(8, s, u), SHIFT_INDEX(9, s, u), SHIFT_INDEX(10, s, u), SHIFT_INDEX(11, s, u), SHIFT_INDEX(12, s, u), SHIFT_INDEX(13, s, u), SHIFT_INDEX(14, s, u), SHIFT_INDEX(15, s, u))
#define LAST_PIXEL_INDEX(i, bpp, used) ((i) < (used) ? (used) - (bpp) + (i) % (bpp) : 16)
#define REPEAT_LAST_PIXEL(v, b, u) SHUFFLE(v, (u8x16) { 0 }, LAST_PIXEL_INDEX(0, b, u), LAST_PIXEL_INDEX(1, b, u), LAST_PIXEL_INDEX(2, b, u), LAST_PIXEL_INDEX(3, b, u), LAST_PIXEL_INDEX(4, b, u), LAST_PIXEL_INDEX(5, b, u), LAST_PIXEL_INDEX(6, b, u), LAST_PIXEL_INDEX(7, b, u), LAST_PIXEL_INDEX(8, b, u), LAST_PIXEL_INDEX(9, b, u), LAST_PIXEL_INDEX(10, b, u), LAST_PIXEL_INDEX(11, b, u), LAST_PIXEL_INDEX(12, b, u), LAST_PIXEL_INDEX(13, b, u), LAST_PIXEL_INDEX(14, b, u), LAST_PIXEL_INDEX(15, b, u))

#define DEFINE_UNFILTER_SUB(BPP)                                             \
    static void unfilter_sub_##BPP(u8* data, size_t size)                    \
    {                                                                        \
        enum { USED = 16 - 16 % (BPP) };                                     \
        u8x16 carry = { 0 };                                                    \
        u8x16 next = { 0 };                                                     \
        size_t i = 0;                                                        \
        if (16 <= size)                                                      \
            next = load16(data);                                             \
        for (; i + 16 <= size; i += USED) {                                  \
            u8x16 v = next;                                                  \
            if (i + USED + 16 <= size)                                       \
                next = load16(data + i + USED);                              \
            v += SHIFT_BYTES_UP(v, (BPP), USED);                             \
            if (2 * (BPP) < USED)                                            \
                v += SHIFT_BYTES_UP(v, 2 * (BPP), USED);                     \
            if (4 * (BPP) < USED)                                            \
                v += SHIFT_BYTES_UP(v, 4 * (BPP), USED);                     \
            if (8 * (BPP) < USED)                                            \
                v += SHIFT_BYTES_UP(v, 8 * (BPP), USED);                     \
            v += carry;                                                      \
            carry = REPEAT_LAST_PIXEL(v, (BPP), USED);                       \
            store16(data + i, v);                                            \
        }                                                                    \
        for (i = i < (BPP) ? (BPP) : i; i < size; ++i)                       \
            data[i] += data[i - (BPP)];                                      \
    }
DEFINE_UNFILTER_SUB(1)
DEFINE_UNFILTER_SUB(2)
DEFINE_UNFILTER_SUB(3)
DEFINE_UNFILTER_SUB(4)
DEFINE_UNFILTER_SUB(6)
DEFINE_UNFILTER_SUB(8)

// One row of Average or Paeth, the left pixel in registers; `bpp` is a constant at each call, so
// the per-channel loop unrolls into straight-line code. PNG rows are always whole pixels.
FP_INLINE void unfilter_average_row(u8* data, u8 const* above, size_t size, unsigned const bpp)
{
    u8 left[8] = { 0 };
    for (size_t i = 0; i < size; i += bpp) {
        for (unsigned k = 0; k < bpp; ++k) {
            left[k] = data[i + k] + (u8)(((u32)left[k] + above[i + k]) >> 1);
            data[i + k] = left[k];
        }
    }
}

FP_INLINE void unfilter_paeth_row(u8* data, u8 const* above, size_t size, unsigned const bpp)
{
    u8 left[8] = { 0 };
    u8 upper_left[8] = { 0 };
    for (size_t i = 0; i < size; i += bpp) {
        for (unsigned k = 0; k < bpp; ++k) {
            u8 const up = above[i + k];
            left[k] = data[i + k] + paeth_predictor(left[k], up, upper_left[k]);
            upper_left[k] = up;
            data[i + k] = left[k];
        }
    }
}

FP_INLINE void unfilter_row_with_pixel_size(filter_type filter, u8* data, u8 const* above, size_t size, unsigned const bpp)
{
    switch (filter) {
    case FILTER_NONE:
        break;
    case FILTER_SUB:
        switch (bpp) {
        case 1: unfilter_sub_1(data, size); break;
        case 2: unfilter_sub_2(data, size); break;
        case 3: unfilter_sub_3(data, size); break;
        case 4: unfilter_sub_4(data, size); break;
        case 6: unfilter_sub_6(data, size); break;
        default: unfilter_sub_8(data, size); break;
        }
        break;
    case FILTER_UP:
        for (size_t i = 0; i < size; ++i)
            data[i] += above[i];
        break;
    case FILTER_AVERAGE:
        unfilter_average_row(data, above, size, bpp);
        break;
    case FILTER_PAETH:
        unfilter_paeth_row(data, above, size, bpp);
        break;
    }
}

static void unfilter_row(filter_type filter, u8* data, u8 const* above, size_t size, unsigned bpp)
{
    // PNG's pixel sizes: one to four channels of 8 or 16 bits (and 1 for lower bit depths).
    switch (bpp) {
    case 1: unfilter_row_with_pixel_size(filter, data, above, size, 1); break;
    case 2: unfilter_row_with_pixel_size(filter, data, above, size, 2); break;
    case 3: unfilter_row_with_pixel_size(filter, data, above, size, 3); break;
    case 4: unfilter_row_with_pixel_size(filter, data, above, size, 4); break;
    case 6: unfilter_row_with_pixel_size(filter, data, above, size, 6); break;
    default: unfilter_row_with_pixel_size(filter, data, above, size, 8); break;
    }
}

// Average makes each byte wait for the one a pixel to its left, so a row is one chain of adds per
// channel; with few channels the chains are short of work. Two rows, the second a pixel behind the
// first (whose bytes it reads as the row above), are twice the chains in the same loop.
FP_INLINE void unfilter_average_two_rows(u8* first, u8* second, u8 const* above, size_t size, unsigned const bpp)
{
    u8 left_in_first[4] = { 0 };
    u8 left_in_second[4] = { 0 };
    for (unsigned k = 0; k < bpp; ++k) {
        left_in_first[k] = first[k] + (u8)(above[k] >> 1);
        first[k] = left_in_first[k];
    }
    for (size_t i = bpp; i < size; i += bpp) {
        for (unsigned k = 0; k < bpp; ++k) {
            left_in_first[k] = first[i + k] + (u8)(((u32)left_in_first[k] + above[i + k]) >> 1);
            first[i + k] = left_in_first[k];
            size_t const j = i - bpp + k;
            left_in_second[k] = second[j] + (u8)(((u32)left_in_second[k] + first[j]) >> 1);
            second[j] = left_in_second[k];
        }
    }
    for (unsigned k = 0; k < bpp; ++k) {
        size_t const j = size - bpp + k;
        second[j] += (u8)(((u32)left_in_second[k] + first[j]) >> 1);
    }
}

FP_INLINE u32 load_pixel(u8 const* pixel, unsigned const bpp)
{
    u32 value = 0;
    memcpy(&value, pixel, bpp);
    return value;
}

FP_INLINE u8x16 diagonal_predict(u8x16 left, u8x16 up, u8x16 upper_left, bool const paeth)
{
    return paeth ? paeth_predictor16(left, up, upper_left) : average_floor8(left, up);
}

// Four rows' pixels for one diagonal step, from row `first_row` on, with lanes outside the image
// zero (and `inside` saying which ones are in).
FP_INLINE u8x16 load_four(u8* const* rows, size_t first_row, size_t x, size_t width, bool const at_edge, unsigned const bpp, u8x16* inside)
{
    u32x4 data;
    u32x4 in;
    for (size_t i = 0; i < 4; ++i) {
        size_t const r = first_row + i;
        bool const is_inside = !at_edge || (x >= r && x - r < width);
        // A 3-byte pixel loaded as 4 bytes reads its row's next pixel's first byte: one load
        // instead of two, for all but a row's last pixel.
        data[i] = is_inside ? (at_edge ? load_pixel(rows[r] + (x - r) * bpp, bpp) : load_u32(rows[r] + (x - r) * bpp)) : 0;
        in[i] = is_inside ? ~0u : 0;
    }
    *inside = (u8x16)in;
    return (u8x16)data;
}

FP_INLINE void store_four(u8* const* rows, size_t first_row, size_t x, size_t width, bool const at_edge, unsigned const bpp, u8x16 result)
{
    u32x4 const by_row = (u32x4)result;
    for (size_t i = 0; i < 4; ++i) {
        size_t const r = first_row + i;
        if (!at_edge || (x >= r && x - r < width)) {
            u32 const pixel = by_row[i];
            memcpy(rows[r] + (x - r) * bpp, &pixel, bpp);
        }
    }
}

// One step of the diagonal: in step x, row r does pixel x - r. A pixel's left neighbour is its
// row's result from the previous step; the pixel above it is the previous row's result from the
// previous step (for the first row, from `above`); and the one above and left is what was above
// it in the previous step. A 3-byte pixel takes 4 bytes of the vector, the fourth unused. In the
// first and last steps, the rows that are outside the image get zeros: those are what's to the
// left of a row's first pixel, and above-left of the next row's. Each vector's state is in its
// own variable: as arrays of vectors, they stayed on the stack.
FP_INLINE void diagonal_step(u8* const* rows, u8 const* above, size_t width, size_t x, bool const at_edge, bool const paeth, unsigned const bpp, unsigned const vectors, u8x16* left_0, u8x16* upper_left_0, u8x16* left_1, u8x16* upper_left_1)
{
    u32 const first_rows_above = !at_edge ? load_u32(above + x * bpp) : (x < width ? load_pixel(above + x * bpp, bpp) : 0);
    u32x4 const left_by_row_0 = (u32x4)*left_0;
    u8x16 const up_0 = (u8x16)(u32x4) { first_rows_above, left_by_row_0[0], left_by_row_0[1], left_by_row_0[2] };
    u8x16 inside_0;
    u8x16 result_0 = load_four(rows, 0, x, width, at_edge, bpp, &inside_0) + diagonal_predict(*left_0, up_0, *upper_left_0, paeth);
    if (at_edge)
        result_0 &= inside_0;

    if (vectors == 2) {
        // The second vector's first row is below the first vector's last: a shuffle, not a lane
        // extraction, which went through the stack.
        u8x16 const up_1 = (u8x16)SHUFFLE((u32x4)*left_1, left_by_row_0, 7, 0, 1, 2);
        u8x16 inside_1;
        u8x16 result_1 = load_four(rows, 4, x, width, at_edge, bpp, &inside_1) + diagonal_predict(*left_1, up_1, *upper_left_1, paeth);
        if (at_edge)
            result_1 &= inside_1;
        store_four(rows, 4, x, width, at_edge, bpp, result_1);
        *left_1 = result_1;
        *upper_left_1 = up_1;
    }
    store_four(rows, 0, x, width, at_edge, bpp, result_0);
    *left_0 = result_0;
    *upper_left_0 = up_0;
}

// Undoes Average or Paeth on 4 * `vectors` rows of `width` 3- or 4-byte pixels, on the diagonal.
FP_INLINE void unfilter_rows_diagonally(u8* const* rows, u8 const* above, size_t width, bool const paeth, unsigned const bpp, unsigned const vectors)
{
    size_t const row_count = 4 * vectors;
    u8x16 left_0 = { 0 }, upper_left_0 = { 0 }, left_1 = { 0 }, upper_left_1 = { 0 };
    // The middle steps have all rows inside the image, and (for 3-byte pixels) a next pixel after
    // each one loaded.
    size_t const middle_end = bpp == 3 ? width - 1 : width;
    size_t x = 0;
    for (; x < row_count - 1; ++x)
        diagonal_step(rows, above, width, x, true, paeth, bpp, vectors, &left_0, &upper_left_0, &left_1, &upper_left_1);
    for (; x < middle_end; ++x)
        diagonal_step(rows, above, width, x, false, paeth, bpp, vectors, &left_0, &upper_left_0, &left_1, &upper_left_1);
    for (; x < width + row_count - 1; ++x)
        diagonal_step(rows, above, width, x, true, paeth, bpp, vectors, &left_0, &upper_left_0, &left_1, &upper_left_1);
}

FP_NOINLINE void unfilter_8_paeth_3(u8* const* rows, u8 const* above, size_t width) { unfilter_rows_diagonally(rows, above, width, true, 3, 2); }
FP_NOINLINE void unfilter_8_paeth_4(u8* const* rows, u8 const* above, size_t width) { unfilter_rows_diagonally(rows, above, width, true, 4, 2); }
FP_NOINLINE void unfilter_4_paeth_3(u8* const* rows, u8 const* above, size_t width) { unfilter_rows_diagonally(rows, above, width, true, 3, 1); }
FP_NOINLINE void unfilter_4_paeth_4(u8* const* rows, u8 const* above, size_t width) { unfilter_rows_diagonally(rows, above, width, true, 4, 1); }
FP_NOINLINE void unfilter_4_average_3(u8* const* rows, u8 const* above, size_t width) { unfilter_rows_diagonally(rows, above, width, false, 3, 1); }
FP_NOINLINE void unfilter_4_average_4(u8* const* rows, u8 const* above, size_t width) { unfilter_rows_diagonally(rows, above, width, false, 4, 1); }

// Consecutive rows of `size` bytes, each one's row above being the one before it (the first's,
// `above`). Takes the rows greedily, several at once where the filters allow. Measured on random
// rows of 4096 pixels: four rows at once are ~2x (Average) and ~1.7-1.9x (Paeth) as fast as one at
// a time for 3- and 4-byte pixels; eight rows of Paeth ~3.1x, while eight of Average were no faster
// than four. Two rows of Average are 1.85x for 1-byte pixels, 1.66x for 2-byte ones, 1.19x for
// 3-byte ones.
FP_INLINE bool all_filters_are(filter_type const* filters, size_t count, filter_type filter)
{
    for (size_t i = 0; i < count; ++i) {
        if (filters[i] != filter)
            return false;
    }
    return true;
}

static void unfilter_rows(filter_type const* filters, u8** rows, size_t count, u8 const* above, size_t size, unsigned bpp)
{
    for (size_t i = 0; i < count;) {
        u8 const* const row_above = i == 0 ? above : rows[i - 1];
        if (bpp == 3 || bpp == 4) {
            size_t const width = size / bpp;
            if (i + 8 <= count && all_filters_are(filters + i, 8, FILTER_PAETH)) {
                (bpp == 3 ? unfilter_8_paeth_3 : unfilter_8_paeth_4)(rows + i, row_above, width);
                i += 8;
                continue;
            }
            if (i + 4 <= count && all_filters_are(filters + i, 4, FILTER_PAETH)) {
                (bpp == 3 ? unfilter_4_paeth_3 : unfilter_4_paeth_4)(rows + i, row_above, width);
                i += 4;
                continue;
            }
            if (i + 4 <= count && all_filters_are(filters + i, 4, FILTER_AVERAGE)) {
                (bpp == 3 ? unfilter_4_average_3 : unfilter_4_average_4)(rows + i, row_above, width);
                i += 4;
                continue;
            }
        }
        if (bpp <= 3 && i + 2 <= count && filters[i] == FILTER_AVERAGE && filters[i + 1] == FILTER_AVERAGE) {
            if (bpp == 1)
                unfilter_average_two_rows(rows[i], rows[i + 1], row_above, size, 1);
            else if (bpp == 2)
                unfilter_average_two_rows(rows[i], rows[i + 1], row_above, size, 2);
            else
                unfilter_average_two_rows(rows[i], rows[i + 1], row_above, size, 3);
            i += 2;
            continue;
        }
        unfilter_row(filters[i], rows[i], row_above, size, bpp);
        ++i;
    }
}

// ================================================================================================
// 5. Unpacking
// ================================================================================================
//
// Each unfiltered row becomes 4-byte pixels right away. 8-bit layouts move with one shuffle per
// 16 bytes (RGB to RGBA puts four pixels in place with the alpha bytes from an all-0xff vector);
// low bit depths and palettes go through 256-entry tables of finished pixels, which also take
// care of scaling 1/2/4-bit grays up and of tRNS transparency.

typedef enum {
    COLOR_GRAY = 0,
    COLOR_RGB = 2,
    COLOR_PALETTE = 3,
    COLOR_GRAY_ALPHA = 4,
    COLOR_RGBA = 6,
} color_type;

typedef struct {
    color_type color;
    unsigned depth;
    size_t width;
    bool bgra;
    u32 table[256]; // Palette or low-bit-depth grays, as finished pixels.
    unsigned palette_size;
    bool has_transparent_value;
    u16 transparent_gray;
    u8 transparent_samples[6]; // RGB as stored: 3 bytes at 8 bits, 6 big-endian at 16.
} unpacker;

FP_INLINE u32 make_pixel(u32 r, u32 g, u32 b, u32 a, bool bgra)
{
    return bgra ? (a << 24) | (r << 16) | (g << 8) | b : (a << 24) | (b << 16) | (g << 8) | r;
}

static void unpack_gray_8(u8 const* row, u8* out, size_t width)
{
    u8x16 const opaque = splat8(0xff);
    size_t x = 0;
    for (; x + 16 <= width; x += 16) {
        u8x16 const g = load16(row + x);
        store16(out + 4 * x, SHUFFLE(g, opaque, 0, 0, 0, 16, 1, 1, 1, 16, 2, 2, 2, 16, 3, 3, 3, 16));
        store16(out + 4 * x + 16, SHUFFLE(g, opaque, 4, 4, 4, 16, 5, 5, 5, 16, 6, 6, 6, 16, 7, 7, 7, 16));
        store16(out + 4 * x + 32, SHUFFLE(g, opaque, 8, 8, 8, 16, 9, 9, 9, 16, 10, 10, 10, 16, 11, 11, 11, 16));
        store16(out + 4 * x + 48, SHUFFLE(g, opaque, 12, 12, 12, 16, 13, 13, 13, 16, 14, 14, 14, 16, 15, 15, 15, 16));
    }
    for (; x < width; ++x)
        store_u32(out + 4 * x, 0xff000000u | row[x] * 0x010101u);
}

static void unpack_gray_alpha_8(u8 const* row, u8* out, size_t width)
{
    size_t x = 0;
    for (; x + 8 <= width; x += 8) {
        u8x16 const v = load16(row + 2 * x);
        store16(out + 4 * x, SHUFFLE(v, v, 0, 0, 0, 1, 2, 2, 2, 3, 4, 4, 4, 5, 6, 6, 6, 7));
        store16(out + 4 * x + 16, SHUFFLE(v, v, 8, 8, 8, 9, 10, 10, 10, 11, 12, 12, 12, 13, 14, 14, 14, 15));
    }
    for (; x < width; ++x)
        store_u32(out + 4 * x, ((u32)row[2 * x + 1] << 24) | row[2 * x] * 0x010101u);
}

// Four pixels at a time: each 16-byte load holds four RGB pixels and four bytes of the next ones,
// which are read again with the next four. The loads stop while 16 bytes of the row are left.
FP_INLINE void unpack_rgb_8(u8 const* row, u8* out, size_t width, bool const bgra)
{
    u8x16 const opaque = splat8(0xff);
    size_t x = 0;
    for (; x * 3 + 16 <= width * 3; x += 4) {
        u8x16 const rgb = load16(row + 3 * x);
        if (bgra)
            store16(out + 4 * x, SHUFFLE(rgb, opaque, 2, 1, 0, 16, 5, 4, 3, 16, 8, 7, 6, 16, 11, 10, 9, 16));
        else
            store16(out + 4 * x, SHUFFLE(rgb, opaque, 0, 1, 2, 16, 3, 4, 5, 16, 6, 7, 8, 16, 9, 10, 11, 16));
    }
    for (; x < width; ++x)
        store_u32(out + 4 * x, make_pixel(row[3 * x], row[3 * x + 1], row[3 * x + 2], 0xff, bgra));
}

FP_INLINE void unpack_rgba_8(u8 const* row, u8* out, size_t width, bool const bgra)
{
    if (!bgra) {
        memcpy(out, row, width * 4);
        return;
    }
    size_t x = 0;
    for (; x + 4 <= width; x += 4) {
        u8x16 const rgba = load16(row + 4 * x);
        store16(out + 4 * x, SHUFFLE(rgba, rgba, 2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15));
    }
    for (; x < width; ++x) {
        u32 const rgba = load_u32(row + 4 * x);
        store_u32(out + 4 * x, (rgba & 0xff00ff00) | ((rgba & 0xff) << 16) | ((rgba >> 16) & 0xff));
    }
}

// 1-, 2- or 4-bit samples, packed from each byte's high bits down, through the table.
FP_INLINE void unpack_low_bit_depth(u8 const* row, u8* out, size_t width, u32 const* table, unsigned const depth)
{
    unsigned const per_byte = 8 / depth;
    unsigned const mask = (1u << depth) - 1;
    size_t x = 0;
    for (; x + per_byte <= width; x += per_byte) {
        u8 const byte = row[x / per_byte];
        for (unsigned k = 0; k < per_byte; ++k)
            store_u32(out + 4 * (x + k), table[(byte >> (8 - depth * (k + 1))) & mask]);
    }
    for (; x < width; ++x)
        store_u32(out + 4 * x, table[(row[x / per_byte] >> (8 - depth * (x % per_byte + 1))) & mask]);
}

FP_NOINLINE void unpack_low_bit_depth_1(u8 const* row, u8* out, size_t width, u32 const* table) { unpack_low_bit_depth(row, out, width, table, 1); }
FP_NOINLINE void unpack_low_bit_depth_2(u8 const* row, u8* out, size_t width, u32 const* table) { unpack_low_bit_depth(row, out, width, table, 2); }
FP_NOINLINE void unpack_low_bit_depth_4(u8 const* row, u8* out, size_t width, u32 const* table) { unpack_low_bit_depth(row, out, width, table, 4); }

static void unpack_through_table(u8 const* row, u8* out, size_t width, u32 const* table, unsigned depth)
{
    if (depth == 1)
        unpack_low_bit_depth_1(row, out, width, table);
    else if (depth == 2)
        unpack_low_bit_depth_2(row, out, width, table);
    else
        unpack_low_bit_depth_4(row, out, width, table);
}

static bool palette_indices_valid(u8 const* row, size_t width, unsigned depth, unsigned palette_size)
{
    unsigned const per_byte = 8 / depth;
    unsigned const mask = (1u << depth) - 1;
    for (size_t x = 0; x < width; ++x) {
        unsigned const sample = depth == 8 ? row[x] : (row[x / per_byte] >> (8 - depth * (x % per_byte + 1))) & mask;
        if (sample >= palette_size)
            return false;
    }
    return true;
}

FP_INLINE int unpack_row_in_format(unpacker const* u, u8 const* row, u8* out, bool const bgra)
{
    size_t const width = u->width;
    switch (u->color) {
    case COLOR_GRAY:
        if (u->depth < 8) {
            unpack_through_table(row, out, width, u->table, u->depth);
        } else if (u->depth == 8 && !u->has_transparent_value) {
            unpack_gray_8(row, out, width);
        } else {
            unsigned const stride = u->depth / 8;
            for (size_t x = 0; x < width; ++x) {
                u8 const* sample = row + x * stride;
                u16 const value = stride == 2 ? (sample[0] << 8) | sample[1] : sample[0];
                u32 const alpha = u->has_transparent_value && value == u->transparent_gray ? 0 : 0xff;
                store_u32(out + 4 * x, (alpha << 24) | sample[0] * 0x010101u);
            }
        }
        return FASTPNG_OK;
    case COLOR_GRAY_ALPHA:
        if (u->depth == 8) {
            unpack_gray_alpha_8(row, out, width);
        } else {
            for (size_t x = 0; x < width; ++x)
                store_u32(out + 4 * x, ((u32)row[4 * x + 2] << 24) | row[4 * x] * 0x010101u);
        }
        return FASTPNG_OK;
    case COLOR_RGB:
        if (u->has_transparent_value) {
            unsigned const stride = u->depth / 8;
            for (size_t x = 0; x < width; ++x) {
                u8 const* sample = row + x * 3 * stride;
                u32 const alpha = memcmp(sample, u->transparent_samples, 3 * stride) == 0 ? 0 : 0xff;
                store_u32(out + 4 * x, make_pixel(sample[0], sample[stride], sample[2 * stride], alpha, bgra));
            }
        } else if (u->depth == 8) {
            unpack_rgb_8(row, out, width, bgra);
        } else {
            for (size_t x = 0; x < width; ++x)
                store_u32(out + 4 * x, make_pixel(row[6 * x], row[6 * x + 2], row[6 * x + 4], 0xff, bgra));
        }
        return FASTPNG_OK;
    case COLOR_RGBA:
        if (u->depth == 8) {
            unpack_rgba_8(row, out, width, bgra);
        } else {
            for (size_t x = 0; x < width; ++x)
                store_u32(out + 4 * x, make_pixel(row[8 * x], row[8 * x + 2], row[8 * x + 4], row[8 * x + 6], bgra));
        }
        return FASTPNG_OK;
    case COLOR_PALETTE:
        if (u->depth == 8) {
            // Checked a row at a time, so the lookup loop has no branch.
            u8 largest = 0;
            for (size_t x = 0; x < width; ++x)
                largest = row[x] > largest ? row[x] : largest;
            if (largest >= u->palette_size)
                return FASTPNG_ERROR_BAD_PALETTE;
            for (size_t x = 0; x < width; ++x)
                store_u32(out + 4 * x, u->table[row[x]]);
        } else {
            // A palette as big as the bit depth allows can't be indexed past.
            if (u->palette_size < (1u << u->depth) && !palette_indices_valid(row, width, u->depth, u->palette_size))
                return FASTPNG_ERROR_BAD_PALETTE;
            unpack_through_table(row, out, width, u->table, u->depth);
        }
        return FASTPNG_OK;
    }
    return FASTPNG_ERROR_BAD_HEADER;
}

static int unpack_row(unpacker const* u, u8 const* row, u8* out)
{
    return u->bgra ? unpack_row_in_format(u, row, out, true) : unpack_row_in_format(u, row, out, false);
}

// ================================================================================================
// 6. The PNG container
// ================================================================================================

static u8 const png_signature[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };

typedef struct {
    u32 width;
    u32 height;
    unsigned depth;
    color_type color;
    bool interlaced;
    unsigned channels;
    u8 const* palette; // PLTE: RGB triples.
    unsigned palette_size;
    u8 const* transparency; // tRNS.
    unsigned transparency_size;
    // The image data: IDAT chunks' contents, in order. When there's one, it's used in place.
    u8 const* first_idat;
    size_t first_idat_size;
    size_t idat_count;
    size_t idat_total;
} png_header;

// Steps to the next chunk: its type, data and length. False at the end of the file, or at a chunk
// that runs past it.
static bool next_chunk(u8 const** p, u8 const* end, u32* type, u8 const** data, u32* length)
{
    if (end - *p < 12)
        return false;
    u32 const chunk_length = read_be32(*p);
    if (chunk_length > (size_t)(end - *p) - 12)
        return false;
    *type = read_be32(*p + 4);
    *data = *p + 8;
    *length = chunk_length;
    *p += 12 + chunk_length;
    return true;
}

#define CHUNK(a, b, c, d) (((u32)(a) << 24) | ((u32)(b) << 16) | ((u32)(c) << 8) | (u32)(d))

static int parse_header(u8 const* png, size_t png_size, png_header* h)
{
    memset(h, 0, sizeof(*h));
    if (png_size < 8 + 25 || memcmp(png, png_signature, 8) != 0)
        return FASTPNG_ERROR_NOT_PNG;
    if (read_be32(png + 8) != 13 || read_be32(png + 12) != CHUNK('I', 'H', 'D', 'R'))
        return FASTPNG_ERROR_BAD_HEADER;
    u8 const* ihdr = png + 16;
    h->width = read_be32(ihdr);
    h->height = read_be32(ihdr + 4);
    h->depth = ihdr[8];
    h->color = (color_type)ihdr[9];
    if (h->width == 0 || h->height == 0 || h->width > 0x7fffffff || h->height > 0x7fffffff)
        return FASTPNG_ERROR_BAD_HEADER;
    if (ihdr[10] != 0 || ihdr[11] != 0 || ihdr[12] > 1)
        return FASTPNG_ERROR_BAD_HEADER;
    h->interlaced = ihdr[12] == 1;
    unsigned const d = h->depth;
    switch (h->color) {
    case COLOR_GRAY:
        h->channels = 1;
        if (d != 1 && d != 2 && d != 4 && d != 8 && d != 16)
            return FASTPNG_ERROR_BAD_HEADER;
        break;
    case COLOR_PALETTE:
        h->channels = 1;
        if (d != 1 && d != 2 && d != 4 && d != 8)
            return FASTPNG_ERROR_BAD_HEADER;
        break;
    case COLOR_RGB:
        h->channels = 3;
        goto eight_or_sixteen;
    case COLOR_GRAY_ALPHA:
        h->channels = 2;
        goto eight_or_sixteen;
    case COLOR_RGBA:
        h->channels = 4;
    eight_or_sixteen:
        if (d != 8 && d != 16)
            return FASTPNG_ERROR_BAD_HEADER;
        break;
    default:
        return FASTPNG_ERROR_BAD_HEADER;
    }
    return FASTPNG_OK;
}

// Reads the chunks up to IEND; with `crc_tables`, checks each one's CRC (of its type and data).
static int parse_chunks(u8 const* png, size_t png_size, png_header* h, crc32_tables const* crc_tables)
{
    int const error = parse_header(png, png_size, h);
    if (error)
        return error;
    u8 const* p = png + 8;
    u32 type, length;
    u8 const* data;
    while (next_chunk(&p, png + png_size, &type, &data, &length)) {
        if (crc_tables && ~crc32_update(crc_tables, ~0u, data - 4, length + 4) != read_be32(data + length))
            return FASTPNG_ERROR_BAD_CHECKSUM;
        if (type == CHUNK('I', 'E', 'N', 'D'))
            break;
        switch (type) {
        case CHUNK('P', 'L', 'T', 'E'):
            h->palette = data;
            h->palette_size = length / 3 > 256 ? 256 : length / 3;
            break;
        case CHUNK('t', 'R', 'N', 'S'):
            h->transparency = data;
            h->transparency_size = length;
            break;
        case CHUNK('I', 'D', 'A', 'T'):
            if (h->idat_count++ == 0) {
                h->first_idat = data;
                h->first_idat_size = length;
            }
            h->idat_total += length;
            break;
        }
    }
    if (h->idat_count == 0)
        return FASTPNG_ERROR_TRUNCATED;
    if (h->color == COLOR_PALETTE && !h->palette)
        return FASTPNG_ERROR_BAD_PALETTE;
    return FASTPNG_OK;
}

// The image data as one zlib stream: the IDAT chunk itself when there's one, else a copy of all
// of them joined (most encoders write many chunks of a few KB; an inflater reading segmented
// input would save this copy, about 1-2% of the decode).
static u8 const* gather_image_data(u8 const* png, size_t png_size, png_header const* h, u8** to_free)
{
    *to_free = NULL;
    if (h->idat_count == 1)
        return h->first_idat;
    u8* joined = malloc(h->idat_total ? h->idat_total : 1);
    if (!joined)
        return NULL;
    size_t at = 0;
    u8 const* p = png + 8;
    u32 type, length;
    u8 const* data;
    while (next_chunk(&p, png + png_size, &type, &data, &length) && type != CHUNK('I', 'E', 'N', 'D')) {
        if (type == CHUNK('I', 'D', 'A', 'T')) {
            memcpy(joined + at, data, length);
            at += length;
        }
    }
    *to_free = joined;
    return joined;
}

static void build_unpacker(png_header const* h, bool bgra, unpacker* u)
{
    memset(u, 0, sizeof(*u));
    u->color = h->color;
    u->depth = h->depth;
    u->width = h->width;
    u->bgra = bgra;

    if (h->color == COLOR_PALETTE) {
        u->palette_size = h->palette_size;
        for (unsigned i = 0; i < 256; ++i) {
            if (i < h->palette_size) {
                u8 const* rgb = h->palette + 3 * i;
                u32 const alpha = i < h->transparency_size ? h->transparency[i] : 0xff;
                u->table[i] = make_pixel(rgb[0], rgb[1], rgb[2], alpha, bgra);
            } else {
                u->table[i] = make_pixel(0, 0, 0, 0xff, bgra);
            }
        }
    }

    // tRNS gives a gray image's transparent gray as one big-endian 16-bit sample, and an RGB
    // image's transparent color as three; an 8-bit image's samples are their low bytes.
    if (h->color == COLOR_GRAY && h->transparency_size >= 2) {
        u->has_transparent_value = true;
        u->transparent_gray = (h->transparency[0] << 8) | h->transparency[1];
    }
    if (h->color == COLOR_RGB && h->transparency_size >= 6) {
        u->has_transparent_value = true;
        if (h->depth == 8) {
            u->transparent_samples[0] = h->transparency[1];
            u->transparent_samples[1] = h->transparency[3];
            u->transparent_samples[2] = h->transparency[5];
        } else {
            memcpy(u->transparent_samples, h->transparency, 6);
        }
    }

    // A 1-, 2- or 4-bit gray image has at most 16 grays: a table scales them up to 8 bits (the
    // largest sample is white; 255 is a multiple of 1, 3 and 15) and applies the transparent gray.
    if (h->color == COLOR_GRAY && h->depth < 8) {
        u32 const largest = (1u << h->depth) - 1;
        for (u32 value = 0; value <= largest; ++value) {
            u32 const gray = value * (0xff / largest);
            u32 const alpha = u->has_transparent_value && value == u->transparent_gray ? 0 : 0xff;
            u->table[value] = (alpha << 24) | gray * 0x010101u;
        }
    }
}

// Rows of one image (or interlace pass) in the inflated data: unfiltered 16 at a time, in place,
// and each unpacked right after. `emit` gets each unpacked row.
#define ROWS_PER_BATCH 16

typedef struct {
    u8* pixels;
    size_t stride;
    // For interlace passes: where a pass's pixel (x, y) goes.
    size_t x0, dx, y0, dy;
    u8* row_buffer; // A pass's unpacked row, before it's spread out.
} row_sink;

// With `adler`, adds each batch of filtered rows to the Adler-32 before unfiltering it in place.
static int decode_rows(u8* data, size_t width, size_t height, size_t row_bytes, unsigned bpp, unpacker* u, row_sink const* sink, bool interlace_pass, adler32_state* adler)
{
    // The row above the first is all zeros.
    u8* zero_row = calloc(row_bytes + 16, 1);
    if (!zero_row)
        return FASTPNG_ERROR_OUT_OF_MEMORY;
    u->width = width;
    u8 const* above = zero_row;
    int error = FASTPNG_OK;
    for (size_t y = 0; y < height && !error; y += ROWS_PER_BATCH) {
        size_t const count = height - y < ROWS_PER_BATCH ? height - y : ROWS_PER_BATCH;
        filter_type filters[ROWS_PER_BATCH];
        u8* rows[ROWS_PER_BATCH];
        for (size_t i = 0; i < count; ++i) {
            u8* const filtered = data + (y + i) * (row_bytes + 1);
            if (filtered[0] > FILTER_PAETH) {
                error = FASTPNG_ERROR_BAD_ZLIB;
                break;
            }
            filters[i] = (filter_type)filtered[0];
            rows[i] = filtered + 1;
        }
        if (error)
            break;
        if (adler)
            adler32_update(adler, data + y * (row_bytes + 1), count * (row_bytes + 1));
        unfilter_rows(filters, rows, count, above, row_bytes, bpp);
        above = rows[count - 1];
        for (size_t i = 0; i < count && !error; ++i) {
            size_t const out_y = sink->y0 + (y + i) * sink->dy;
            if (!interlace_pass) {
                error = unpack_row(u, rows[i], sink->pixels + out_y * sink->stride);
                continue;
            }
            error = unpack_row(u, rows[i], sink->row_buffer);
            u8* const out_row = sink->pixels + out_y * sink->stride;
            for (size_t x = 0; x < width; ++x)
                memcpy(out_row + 4 * (sink->x0 + x * sink->dx), sink->row_buffer + 4 * x, 4);
        }
    }
    free(zero_row);
    return error;
}

// Sizes are worked out in 64 bits whatever size_t is (a width can be 2^31 - 1 pixels of 8 bytes),
// and checked against this before anything is allocated: 1 TiB, or less than half the address
// space, so adding a margin to a size that passed can't overflow either.
#define MAX_BUFFER_BYTES ((u64)SIZE_MAX / 4 < ((u64)1 << 40) ? (u64)SIZE_MAX / 4 : ((u64)1 << 40))

static u64 row_bytes_for(u64 width, png_header const* h)
{
    return (width * h->channels * h->depth + 7) / 8;
}

static u8 const adam7_x0[7] = { 0, 4, 0, 2, 0, 1, 0 };
static u8 const adam7_y0[7] = { 0, 0, 4, 0, 2, 0, 1 };
static u8 const adam7_dx[7] = { 8, 8, 4, 4, 2, 2, 1 };
static u8 const adam7_dy[7] = { 8, 8, 8, 4, 4, 2, 2 };

static size_t pass_size(u32 full, unsigned start, unsigned step)
{
    return full > start ? (full - start + step - 1) / step : 0;
}

// The size of the filtered rows, each with its filter byte, and of the decoded pixels;
// FASTPNG_ERROR_TOO_LARGE when either is more than fastpng allocates.
static int image_sizes(png_header const* h, u64* inflated_size, u64* pixels_size)
{
    u64 inflated = 0;
    if (!h->interlaced) {
        if (__builtin_mul_overflow(row_bytes_for(h->width, h) + 1, (u64)h->height, &inflated))
            return FASTPNG_ERROR_TOO_LARGE;
    } else {
        for (int pass = 0; pass < 7; ++pass) {
            u64 const w = pass_size(h->width, adam7_x0[pass], adam7_dx[pass]);
            u64 const ph = pass_size(h->height, adam7_y0[pass], adam7_dy[pass]);
            u64 pass_bytes;
            if (w && ph && (__builtin_mul_overflow(row_bytes_for(w, h) + 1, ph, &pass_bytes) || __builtin_add_overflow(inflated, pass_bytes, &inflated)))
                return FASTPNG_ERROR_TOO_LARGE;
        }
    }
    u64 pixels;
    if (__builtin_mul_overflow((u64)h->width * 4, (u64)h->height, &pixels))
        return FASTPNG_ERROR_TOO_LARGE;
    if (inflated > MAX_BUFFER_BYTES || pixels > MAX_BUFFER_BYTES)
        return FASTPNG_ERROR_TOO_LARGE;
    *inflated_size = inflated;
    *pixels_size = pixels;
    return FASTPNG_OK;
}

int fastpng_get_size(void const* png, size_t png_size, uint32_t* width, uint32_t* height)
{
    png_header h;
    int error = parse_header(png, png_size, &h);
    u64 inflated_size, pixels_size;
    if (!error)
        error = image_sizes(&h, &inflated_size, &pixels_size);
    if (error)
        return error;
    *width = h.width;
    *height = h.height;
    return FASTPNG_OK;
}

// Parses the chunks and works out the sizes of the filtered rows and of the pixels; rejects
// images whose data can't be there, before anything big is allocated. The sizes fit in size_t,
// and so does everything smaller computed from them (row sizes, offsets into the rows).
static int parse_and_size(u8 const* png, size_t png_size, png_header* h, size_t* inflated_size, size_t* pixels_size, crc32_tables const* crc_tables)
{
    int error = parse_chunks(png, png_size, h, crc_tables);
    if (error)
        return error;
    u64 inflated, pixels;
    if ((error = image_sizes(h, &inflated, &pixels)))
        return error;
    // Deflate expands at most 1032:1 (a 258-byte match in two bits, roughly), so a few bytes of
    // image data can't claim gigapixels.
    if (inflated / 1032 > (u64)h->idat_total + 64)
        return FASTPNG_ERROR_TRUNCATED;
    *inflated_size = (size_t)inflated;
    *pixels_size = (size_t)pixels;
    return FASTPNG_OK;
}

int fastpng_decode(void const* png_bytes, size_t png_size, fastpng_format format, void* pixels, size_t stride, unsigned flags)
{
    if (!png_bytes || !pixels)
        return FASTPNG_ERROR_BAD_ARGUMENT;
    u8 const* const png = png_bytes;
    crc32_tables crc_tables;
    if (flags & FASTPNG_CHECK_CRC)
        crc32_init(&crc_tables);
    png_header h;
    size_t inflated_size, pixels_size;
    int error = parse_and_size(png, png_size, &h, &inflated_size, &pixels_size, (flags & FASTPNG_CHECK_CRC) ? &crc_tables : NULL);
    if (error)
        return error;
    if ((u64)stride < (u64)h.width * 4)
        return FASTPNG_ERROR_BAD_ARGUMENT;

    u8* joined;
    u8 const* zlib = gather_image_data(png, png_size, &h, &joined);
    if (!zlib)
        return FASTPNG_ERROR_OUT_OF_MEMORY;
    size_t const zlib_size = h.idat_count == 1 ? h.first_idat_size : h.idat_total;

    // The zlib wrapper: deflate, no preset dictionary, and at the end the inflated data's Adler-32.
    u8* inflated = NULL;
    if (zlib_size < 2 || (zlib[0] & 0x0f) != 8 || (zlib[0] >> 4) > 7 || ((zlib[0] << 8) | zlib[1]) % 31 != 0 || (zlib[1] & 0x20)) {
        error = FASTPNG_ERROR_BAD_ZLIB;
        goto done;
    }
    // Room for the fast loop's overrun past the end.
    size_t const capacity = inflated_size + FASTLOOP_BYTES_WRITTEN + 1;
    inflated = malloc(capacity);
    if (!inflated) {
        error = FASTPNG_ERROR_OUT_OF_MEMORY;
        goto done;
    }
    size_t produced = 0;
    size_t deflate_size = 0;
    error = inflate_stream(zlib + 2, zlib_size - 2, inflated, capacity, &produced, &deflate_size);
    if (error)
        goto done;
    if (produced < inflated_size) {
        error = FASTPNG_ERROR_TRUNCATED;
        goto done;
    }

    adler32_state adler_state = { 1, 0 };
    adler32_state* const adler = (flags & FASTPNG_CHECK_ADLER32) ? &adler_state : NULL;
    unpacker u;
    build_unpacker(&h, format == FASTPNG_BGRA, &u);
    unsigned const bpp = (h.depth * h.channels + 7) / 8;
    if (!h.interlaced) {
        row_sink const sink = { pixels, stride, 0, 1, 0, 1, NULL };
        error = decode_rows(inflated, h.width, h.height, (size_t)row_bytes_for(h.width, &h), bpp, &u, &sink, false, adler);
    } else {
        u8* row_buffer = malloc((size_t)h.width * 4 + 16);
        if (!row_buffer) {
            error = FASTPNG_ERROR_OUT_OF_MEMORY;
            goto done;
        }
        u8* data = inflated;
        for (int pass = 0; pass < 7 && !error; ++pass) {
            size_t const w = pass_size(h.width, adam7_x0[pass], adam7_dx[pass]);
            size_t const ph = pass_size(h.height, adam7_y0[pass], adam7_dy[pass]);
            if (!w || !ph)
                continue;
            size_t const row_bytes = (size_t)row_bytes_for(w, &h);
            row_sink const sink = { pixels, stride, adam7_x0[pass], adam7_dx[pass], adam7_y0[pass], adam7_dy[pass], row_buffer };
            error = decode_rows(data, w, ph, row_bytes, bpp, &u, &sink, true, adler);
            data += (row_bytes + 1) * ph;
        }
        free(row_buffer);
    }

    if (!error && adler) {
        // Any data past the image's rows counts too.
        adler32_update(adler, inflated + inflated_size, produced - inflated_size);
        size_t const trailer = 2 + deflate_size;
        if (zlib_size < trailer || zlib_size - trailer < 4)
            error = FASTPNG_ERROR_TRUNCATED;
        else if (read_be32(zlib + trailer) != ((adler->s2 << 16) | adler->s1))
            error = FASTPNG_ERROR_BAD_CHECKSUM;
    }

done:
    free(inflated);
    free(joined);
    return error;
}

uint8_t* fastpng_decode_alloc(void const* png, size_t png_size, fastpng_format format, unsigned flags, uint32_t* width, uint32_t* height, int* error_out)
{
    png_header header = { 0 };
    size_t inflated_size, pixels_size;
    int error = png ? parse_and_size(png, png_size, &header, &inflated_size, &pixels_size, NULL) : FASTPNG_ERROR_BAD_ARGUMENT;
    uint32_t const w = header.width;
    uint32_t const h = header.height;
    uint8_t* pixels = NULL;
    if (!error) {
        if (!(pixels = malloc(pixels_size)))
            error = FASTPNG_ERROR_OUT_OF_MEMORY;
        else if ((error = fastpng_decode(png, png_size, format, pixels, (size_t)w * 4, flags)) != FASTPNG_OK) {
            free(pixels);
            pixels = NULL;
        }
    }
    if (error_out)
        *error_out = error;
    if (pixels) {
        *width = w;
        *height = h;
    }
    return pixels;
}

char const* fastpng_error_string(int error)
{
    switch (error) {
    case FASTPNG_OK: return "ok";
    case FASTPNG_ERROR_NOT_PNG: return "not a PNG file";
    case FASTPNG_ERROR_BAD_HEADER: return "bad or unsupported image header";
    case FASTPNG_ERROR_TOO_LARGE: return "image too large";
    case FASTPNG_ERROR_TRUNCATED: return "truncated data";
    case FASTPNG_ERROR_BAD_ZLIB: return "bad zlib/deflate data";
    case FASTPNG_ERROR_BAD_PALETTE: return "bad palette";
    case FASTPNG_ERROR_OUT_OF_MEMORY: return "out of memory";
    case FASTPNG_ERROR_BAD_ARGUMENT: return "bad argument";
    case FASTPNG_ERROR_BAD_CHECKSUM: return "checksum mismatch";
    }
    return "unknown error";
}
