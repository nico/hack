/*
clang -O2 cab/gunzip.c
*/
#define _CRT_SECURE_NO_WARNINGS
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void fatal(const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);
  exit(1);
}

static uint16_t little_uint16t(uint8_t* d) {
  return d[1] << 8 | d[0];
}

static uint32_t little_uint32t(uint8_t* d) {
  return d[3] << 24 | d[2] << 16 | d[1] << 8 | d[0];
}

// rfc1951 describes the deflate bitstream.
struct Bitstream {
  int curbit;
  size_t curword;
  uint16_t curword_val;
  uint8_t* source;
  size_t source_len;
};

static void bitstream_init(
    struct Bitstream* bs, uint8_t* source, size_t source_len) {
  bs->curbit = 0;
  bs->curword = 0;
  bs->curword_val = little_uint16t(source);
  bs->source = source;
  bs->source_len = source_len;
}

static int bitstream_getbit(struct Bitstream* bs) {
  int bit = (bs->curword_val >> bs->curbit) & 1;
  bs->curbit += 1;
  if (bs->curbit > 15) {
    bs->curbit = 0;
    bs->curword += 2;  // in bytes
    if (bs->curword < bs->source_len)
      bs->curword_val = little_uint16t(bs->source + bs->curword);
  }
  return bit;
}

static int bitstream_getbits(struct Bitstream* bs, int n) {
  // Doing this bit-by-bit is inefficient; this should try to bunch things up.
  int bits = 0;
  for (int i = 0; i < n; ++i)
    // deflate orders bits right-to-left.
    bits = bits | (bitstream_getbit(bs) << i);
  return bits;
}

static void bitstream_parse_uncompressed_block(
    struct Bitstream* bs, uint16_t* size, uint16_t* nsize, uint8_t** data) {
  size_t start = bs->curword;
  if (bs->curbit > 0) ++start;
  if (bs->curbit > 8) ++start;
  *size = little_uint16t(bs->source + start);
  *nsize = little_uint16t(bs->source + start + 2);
  *data = bs->source + start + 4;
  bs->curbit = 0;
  bs->curword = start + 4 + *size;
  if (bs->curword < bs->source_len)
    bs->curword_val = little_uint16t(bs->source + bs->curword);
}

struct HuffTree {
  int last_code[16];
  uint16_t* codes[17];  // XXX 17 needed? (see +1 in hufftree_readsym)
};

static void hufftree_init(
    struct HuffTree* ht, int* nodelengths, int nodecount, uint16_t* storage) {
  // Given the lengths of the nodes in a canonical huffman tree,
  // returns a (last_code, codes) tuple, where last_code[n] returns the
  // last prefix of length n, and codes[n] contains a list of all values of
  // prefixes with length n.
  // The canonical huffman trees match rfc1951.
  int maxlen = 0;
  for (int i = 0; i < nodecount; ++i)
    if (nodelengths[i] > maxlen)
      maxlen = nodelengths[i];
  int bl_count[16] = {0};
  for (int i = 0; i < nodecount; ++i) {
    int len_i = nodelengths[i];
    if (len_i != 0) bl_count[len_i]++;
  }
  ht->codes[0] = &storage[0];
  for (int i = 1; i < 17; ++i)
    ht->codes[i] = ht->codes[i - 1] + bl_count[i - 1];  // XXX i - i lol
  int offs[16] = {0};
  for (int i = 0; i < nodecount; ++i) {
    int len_i = nodelengths[i];
    if (len_i != 0) ht->codes[len_i][offs[len_i]++] = i;
  }
  int code = 0;
  memset(ht->last_code, 0, sizeof(ht->last_code));
  for (int i = 1; i < 16; ++i) {
    code = (code + bl_count[i - 1]) << 1;
    ht->last_code[i] = code + bl_count[i] - 1;
  }
}

static int hufftree_readsym(struct HuffTree* ht, struct Bitstream* bs) {
  int curbits = bitstream_getbit(bs);
  int curlen = 1;
  int* last_code = ht->last_code;
  while (curbits > last_code[curlen]) {
    // Note that this uses reversed bit order compared to bitstream_getbits()
    curbits = (curbits << 1) | bitstream_getbit(bs);
    curlen += 1;
  }
  // Note: 2nd index is negative to index from end of list (hence curlen+1)
  return ht->codes[curlen+1][curbits - last_code[curlen] - 1];
}

struct Window {
  int win_write;
  int win_size;
  uint8_t* window;  // XXX ownership?
};

static void window_init(struct Window* w, int window_size) {
  w->win_write = 0;
  w->win_size = 1 << window_size;
  w->window = malloc(w->win_size);
}

static void window_output_literal(struct Window* w, uint8_t c) {
  if (w->win_write >= w->win_size)
    w->win_write = 0;
  w->window[w->win_write] = c;
  w->win_write++;
}

static void window_output_block(struct Window* w, uint8_t* d, uint16_t size) {
  if (size > w->win_size) {
    d += size - w->win_size;
    size = w->win_size;
  }
  size_t rightspace = w->win_size - w->win_write;
  if (rightspace >= size) {
    memcpy(w->window + w->win_write, d, size);
    w->win_write += size;
  } else {
    size_t leftspace = size - rightspace;
    memcpy(w->window + w->win_write, d, rightspace);
    memcpy(w->window, d + rightspace, leftspace);
    w->win_write = leftspace;
  }
}

static void window_copy_match(
    struct Window* w, int match_offset, int match_length) {
  // match_offset is relative to the end of the window.
  bool no_overlap = w->win_write >= match_offset >= match_length &&
                    w->win_write + match_length < w->win_size;
  match_offset = w->win_write - match_offset;
  if (no_overlap) {
    // Can use faster memcpy().
    // XXX: measure if this actually helps in C.
    memcpy(w->window + w->win_write, w->window + match_offset, match_length);
    w->win_write += match_length;
    return;
  }
  if (match_offset < 0)
    match_offset += w->win_size;
  // Max match length is 258, win size is 32768, match will always fit.
  for (int i = 0; i < match_length; ++i) {
    window_output_literal(w, w->window[match_offset]);  // FIXME: chunkier?
    match_offset += 1;
    if (match_offset >= w->win_size)
      match_offset -= w->win_size;
  }
}

static void window_write_from(struct Window* w, FILE* outfile, int win_start) {
  if (w->win_write >= win_start)
    fwrite(w->window + win_start, 1, w->win_write - win_start, outfile);
  else {
    fwrite(w->window + win_start, 1, w->win_size - win_start, outfile);
    fwrite(w->window, 1, w->win_write, outfile);
  }
}

static void deflate_decode_pretree(struct HuffTree* pretree,
                                   struct Bitstream* bitstream,
                                   int* lengths,
                                   int num_lengths) {
  int i = 0;
  while (i < num_lengths) {
    int code = hufftree_readsym(pretree, bitstream);
    // code 0-15: Len[x] = code
    // 16: for next (3 + getbits(2)) elements, Len[x] = previous code
    // 17: for next (3 + getbits(3)) elements, Len[x] = 0
    // 18: for next (11 + getbits(7)) elements, Len[x] = 0
    if (code <= 15) {
      lengths[i] = code;
      i += 1;
    } else if (code == 16) {
      int n = 3 + bitstream_getbits(bitstream, 2);
      for (int j = i; j < i+n; ++j)
        lengths[j] = lengths[i - 1];
      i += n;
    } else if(code == 17) {
      int n = 3 + bitstream_getbits(bitstream, 3);
      for (int j = i; j < i+n; ++j)
        lengths[j] = 0;
      i += n;
    } else {
      // code == 18
      int n = 11 + bitstream_getbits(bitstream, 7);
      for (int j = i; j < i+n; ++j)
        lengths[j] = 0;
      i += n;
    }
  }
}

int main(int argc, char* argv[]) {
  if (argc <= 1)
    fatal("need filename\n");
  FILE* in = fopen(argv[1], "rb");
  if (!in)
    fatal("failed to open %s\n", argv[1]);

  fseek(in, 0, SEEK_END);
  long size = ftell(in);
  rewind(in);

  uint8_t* gz = (uint8_t*)malloc(size);
  fread(gz, 1, size, in);

  if (size < 10) fatal("file too small\n");

  if (memcmp("\x1f\x8b", gz, 2)) fatal("invalid file header\n");
  if (gz[2] != 8) fatal("unexpected compression method %d\n", gz[2]);
  uint8_t flags = gz[3];
  time_t mtime = little_uint32t(gz + 4);
  uint8_t extra_flags = gz[8];
  uint8_t os = gz[9];
  size_t off = 10;

  printf("flags %d\n", flags);
  printf("mtime %s", ctime(&mtime));  // ctime() result contains trailing \n
  printf("extra_flags %d\n", extra_flags);
  printf("os %d\n", os);

  if (flags & 4) {
    uint32_t extra_size = little_uint32t(gz + off);
    printf("extra size %d\n", extra_size);
    off += 4 + extra_size;
  }

  if (flags & 8) {
    printf("name %s\n", gz + off);
    off += strlen((char*)gz + off) + 1;
  }

  if (flags & 16) {
    printf("comment %s\n", gz + off);
    off += strlen((char*)gz + off) + 1;
  }

  if (flags & 2) {
    printf("header crc16 %d\n", little_uint16t(gz + off));
    off += 2;
  }

  // XXX explain.
  int extra_len_bits[7*4 + 1] = {0};
  for (int i = 0; i < 6*4; ++i)
    extra_len_bits[i + 4] = i/4;
  extra_len_bits[7*4] = 0;
  int base_length = 3;
  int base_lengths[7*4 + 1];
  for (int i = 0; i < 7*4 + 1; ++i) {
    base_lengths[i] = base_length;
    base_length += 1 << extra_len_bits[i];
  }
  // Code 285 is special: It's a 0-extra-bits encoding of the longest-possible
  // value and it means "258", which could also (less efficiently) be coded
  // as code 284 (base position 227) + 31 in the 5 extra bits. The construction
  // in the loop above would assign 259 to code 285 instead.
  base_lengths[7*4] = 258;
  int extra_dist_bits[30] = {0};
  for (int i = 0; i < 28; ++i)
    extra_dist_bits[i + 2] = i/2;
  int base_dist = 1;
  int base_dists[30];
  for (int i = 0; i < 30; ++i) {
    base_dists[i] = base_dist;
    base_dist += 1 << extra_dist_bits[i];
  }

  FILE* outfile = fopen("gunzip.out", "wb");
  struct Bitstream bitstream;
  bitstream_init(&bitstream, gz + off, size - off - 8);
  struct Window window;
  window_init(&window, 15);
  bool is_last_block;
  do {
    is_last_block = bitstream_getbit(&bitstream);
    int block_type = bitstream_getbits(&bitstream, 2);
    if (block_type == 3) fatal("invalid block\n");

    if (block_type == 0) {
      uint16_t size, nsize;
      uint8_t* data;
      bitstream_parse_uncompressed_block(&bitstream, &size, &nsize, &data);
      if (size != (uint16_t)~nsize) fatal("invalid uncompressed header\n");
      fwrite(data, 1, size, outfile);
      window_output_block(&window, data, size);
      continue;
    }

    int lengths[288 + 30];
    int num_literals_lengths;
    int num_distances;
    if (block_type == 2) {
      // dynamic huffman code, read huffman tree description
      num_literals_lengths = bitstream_getbits(&bitstream, 5) + 257;
      num_distances = bitstream_getbits(&bitstream, 5) + 1;
      int num_pretree = bitstream_getbits(&bitstream, 4) + 4;
      int pretree_order[] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
      int pretree_lengths[19], i;
      for (i = 0; i < num_pretree; ++i)
        pretree_lengths[pretree_order[i]] = bitstream_getbits(&bitstream, 3);
      for (; i < 19; ++i)
        pretree_lengths[pretree_order[i]] = 0;

      struct HuffTree pretree; uint16_t pretree_storage[19];
      hufftree_init(&pretree, pretree_lengths, 19, pretree_storage);
      // "The code length repeat codes can cross from HLIT + 257 to the HDIST +
      // 1 code lengths", so we have to use a single list for the huflengths
      // here.
      deflate_decode_pretree(&pretree, &bitstream, lengths,
                             num_literals_lengths + num_distances);
    } else {
      int i = 0;
      for (; i < 144; ++i) lengths[i] = 8;
      for (; i < 256; ++i) lengths[i] = 9;
      for (; i < 280; ++i) lengths[i] = 7;
      for (; i < 288; ++i) lengths[i] = 8;
      num_literals_lengths = 288;
      for (; i < 288 + 30; ++i) lengths[i] = 5;
      num_distances = 30;
    }
    struct HuffTree littree; uint16_t littree_storage[288];
    hufftree_init(&littree, lengths, num_literals_lengths, littree_storage);
    struct HuffTree disttree; uint16_t disttree_storage[30];
    hufftree_init(&disttree,
        lengths + num_literals_lengths, num_distances, disttree_storage);

    int win_start = window.win_write;
    for(;;) {
      int code = hufftree_readsym(&littree, &bitstream);
      if (code == 256) {
        break;
      } else if (code < 256) {
        // literal
        window_output_literal(&window, (uint8_t)code);
        //printf("lit %d\n", code);
      } else if (code > 256) {
        // match. codes 257..285 represent lengths 3..258 (hence some bits might
        // have to follow the mapped code).
        code -= 257;
        int match_len = base_lengths[code] +
                        bitstream_getbits(&bitstream, extra_len_bits[code]);
        int dist = hufftree_readsym(&disttree, &bitstream);
        int match_offset = base_dists[dist] +
                           bitstream_getbits(&bitstream, extra_dist_bits[dist]);
        window_copy_match(&window, match_offset, match_len);
        //printf("match %d %d\n", match_offset, match_len);
      }
      // We could write to disk right after each literal and match, but instead
      // just write every 16kB. Since the max match is 258 bytes, we won't miss
      // data in the window after a long match: long matches are still short
      // compared to the window size.
      if (((window.win_write - win_start) & 0x7fff) > 0x3fff) {
        window_write_from(&window, outfile, win_start);
        win_start = window.win_write;
      }
    }
    window_write_from(&window, outfile, win_start);
  } while (!is_last_block);
  fclose(outfile);

  free(gz);
  fclose(in);
}
