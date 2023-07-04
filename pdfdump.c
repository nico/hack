#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/*
Spec:
https://web.archive.org/web/20181118152616/https://www.adobe.com/content/dam/acom/en/devnet/acrobat/pdfs/pdf_reference_1-7.pdf
or
https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf

(Or jump through hoops at https://pdfa.org/resource/pdf-specification-index/)
*/

static void fatal(const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);
  exit(1);
}

struct Range {
  uint8_t* begin;
  uint8_t* end;
};

struct Span {
  uint8_t* data;
  size_t size;
};

static void span_advance(struct Span* span, size_t s) {
  assert(span->size >= s);
  span->data += s;
  span->size -= s;
}

static bool is_whitespace(uint8_t c) {
  // 3.1.1 Character Set
  // TABLE 3.1 White-space characters
  return c == 0 || c == '\t' || c == '\n' || c == 10 || c == '\r' || c == ' ';
}

static bool is_delimiter(uint8_t c) {
  // 3.1.1 Character Set
  return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' ||
         c == '{' || c == '}' || c == '/' || c == '%';
}

// 3.2 Objects
enum ObjectKind {
  Boolean,
  Integer,
  Real,
  String,  // (hi) or <6869>
  Name,    // /hi
  Array,  // [0 1 2]
  Dictionary,  //  <</Name1 (val1) /Name2 (val2)>>

  // 3.2.7 Steam Objects
  // "All streams must be indirect objects and the stream dictionary must
  //  be a direct object."
  Stream,

  Null,
  IndirectObject,  // 4 0 Obj
};

static void consume_newline(struct Span* data) {
  if (data->size == 0)
    fatal("not enough data for newline\n");

  if (data->data[0] == '\n') {
    span_advance(data, 1);
    return;
  }

  if (data->data[0] != '\r')
    fatal("not a newline\n");

  if (data->size >= 1 && data->data[1] == '\n')
    span_advance(data, 1);
  span_advance(data, 1);
}

static void dump_header(struct Span* data) {
  if (data->size < strlen("%PDF-a.b\n"))
    fatal("not enough data for header\n");

  if (strncmp("%PDF-", (char*)data->data, strlen("%PDF-")))
    fatal("wrong file start: no %%PDF-\n");

  if (data->data[strlen("%PDF-a")] != '.')
    fatal("wrong file start: no dot?\n");

  uint8_t v0 = data->data[strlen("%PDF-")] - '0';
  uint8_t v1 = data->data[strlen("%PDF-a.")] - '0';

  span_advance(data, strlen("%PDF-a.b"));
  consume_newline(data);

  printf("v %d.%d\n", v0, v1);
}

static void dump(struct Span data) {
  // 3.4 File Structure
  dump_header(&data);
}

int main(int argc, char* argv[]) {
  if (argc != 2)
    fatal("expected 1 arg, got %d\n", argc - 1);

  const char* in_name = argv[1];

  // Read input.
  int in_file = open(in_name, O_RDONLY);
  if (!in_file)
    fatal("Unable to read \'%s\'\n", in_name);

  struct stat in_stat;
  if (fstat(in_file, &in_stat))
    fatal("Failed to stat \'%s\'\n", in_name);

  // Casting memory like this is not portable.
  void* contents = mmap(
      /*addr=*/0, in_stat.st_size,
      PROT_READ, MAP_SHARED, in_file, /*offset=*/0);
  if (contents == MAP_FAILED)
    fatal("Failed to mmap: %d (%s)\n", errno, strerror(errno));

  dump((struct Span){ contents, in_stat.st_size });

  munmap(contents, in_stat.st_size);
  close(in_file);
}
