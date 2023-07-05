#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
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

Normally you'd read a PDF using the lookup structures at the end of the file
(trailer and xref), and read only the objects needed for rendering the currently
visible page.
This here is for dumping all the data in a PDF, so it starts at the start and
reads all the data. Normally you wouldn't do that.

ideas:
- crunch
  - (re)compress streams
  - remove spaces between names
  - convert to object streams
  - remove remnants of incremental edits (multiple %EOF, etc)
  - gc unused indirect objects
    - gc old unref'd generetations of indirect objects
  - gc unused direct objects (harder)
  - gc objects that have no visual/selection/a11y effect (even harder)
  - dedup multiple objects with identical contents (?)

- spy
  - show only hidden / unref'd contents

- compile
  - (re)compress streams
  - fill in binary comment at start
  - fill in stream lengths
  - fill in xref table / startxref offset

- linearize / delinearize

- tool
  - pretty print (consistent newlines, indent dict contents, etc)
  - validate input
    - stream /Lengths consistent with contents / offset
    - stream /Lengths consistent with contents / offset
  - extract images
  - extract text (by file order and by paint order on page; and from selection
    data)
  - fill in form data
*/

static noreturn void fatal(const char* msg, ...) {
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

struct SpanReader {
  struct Span span;
  size_t pos;
};

static size_t reader_size(const struct SpanReader* reader) {
  return reader->span.size - reader->pos;
}

static bool reader_empty(const struct SpanReader* reader) {
  return reader_size(reader) == 0;
}

static uint8_t* reader_data(const struct SpanReader* reader) {
  assert(reader->pos + offset < reader->span.size);
  return reader->span.data + reader->pos;
}

static uint8_t reader_get(const struct SpanReader* reader, size_t offset) {
  assert(reader->pos + offset < reader->span.size);
  return reader->span.data[reader->pos + offset];
}

static uint8_t reader_cur(const struct SpanReader* reader) {
  return reader_get(reader, 0);
}

static void reader_advance(struct SpanReader* reader, size_t s) {
  assert(s <= reader_size(reader));
  reader->pos += s;
}

static bool is_whitespace(uint8_t c) {
  // 3.1.1 Character Set
  // TABLE 3.1 White-space characters
  return c == 0 || c == '\t' || c == '\n' || c == 10 || c == '\r' || c == ' ';
}

static bool is_newline(uint8_t c) {
  return c == '\n' || c == '\r';
}

static bool is_delimiter(uint8_t c) {
  // 3.1.1 Character Set
  return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' ||
         c == '{' || c == '}' || c == '/' || c == '%';
}

static bool is_number_start(uint8_t c) {
  return c == '+' || c == '-' || c == '.' || (c >= '0' && c <= '9');
}

static bool is_number_continuation(uint8_t c) {
  return (c >= '0' && c <= '9') || c == '.';
}

static bool is_name_continuation(uint8_t c) {
  // 3.2.4 Name Objects
  // "The name may include any regular characters, but not delimiters or
  //  white-space characters."
  return !is_whitespace(c) && !is_delimiter(c);
}

static bool is_octal_digit(uint8_t c) {
  return c >= '0' && c <= '7';
}

enum TokenKind {
  kw_obj,
  kw_endobj,

  kw_stream,
  kw_endstream,

  kw_R,
  kw_f,
  kw_n,

  kw_xref,

  kw_trailer,
  kw_startxref,

  kw_true,
  kw_false,

  kw_null,

  tok_number,

  tok_dict, // <<
  tok_enddict, // >>

  tok_array, // [
  tok_endarray, // ]

  tok_name, // /asdf
  tok_string, // (hi) or <6869>

  tok_comment, // %

  tok_eof,
};

struct Token {
  enum TokenKind kind;
  struct Span payload;
};

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

static void consume_newline(struct SpanReader* reader) {
  if (reader_empty(reader))
    fatal("not enough data for newline\n");

  if (reader_cur(reader) == '\n') {
    reader_advance(reader, 1);
    return;
  }

  if (reader_cur(reader) != '\r')
    fatal("not a newline\n");

  if (reader_size(reader) > 1 && reader_get(reader, 1) == '\n')
    reader_advance(reader, 1);
  reader_advance(reader, 1);
}

static void dump_header(struct SpanReader* reader) {
  if (reader_size(reader) < strlen("%PDF-a.b\n"))
    fatal("not enough data for header\n");

  if (strncmp("%PDF-", (char*)reader_data(reader), strlen("%PDF-")))
    fatal("wrong file start: no %%PDF-\n");

  if (reader_get(reader, strlen("%PDF-a")) != '.')
    fatal("wrong file start: no dot?\n");

  uint8_t v0 = reader_get(reader, strlen("%PDF-")) - '0';
  uint8_t v1 = reader_get(reader, strlen("%PDF-a.")) - '0';

  reader_advance(reader, strlen("%PDF-a.b"));
  consume_newline(reader);

  printf("v %d.%d\n", v0, v1);
}

static void advance_to_next_token(struct SpanReader* reader) {
  while (!reader_empty(reader) && is_whitespace(reader_cur(reader)))
    reader_advance(reader, 1);
}

static bool is_keyword(const struct SpanReader* reader, const char* keyword) {
  size_t n = strlen(keyword);
  if (reader_size(reader) < n)
    return false;

  if (strncmp((char*)reader_data(reader), keyword, n))
    return false;

  return reader_size(reader) == n || is_whitespace(reader_get(reader, n)) ||
         is_delimiter(reader_get(reader, n));
}

static enum TokenKind classify_current(struct SpanReader* reader) {
  if (reader_empty(reader))
    return tok_eof;

  uint8_t c = reader_cur(reader);
  assert(!is_whitespace(c));

  if (is_number_start(c))
    return tok_number;

  if (c == '<') {
    if (reader_size(reader) > 1 && reader_get(reader, 1) == '<')
      return tok_dict;
    return tok_string;
  }

  if (c == '>') {
    if (reader_size(reader) > 1 && reader_get(reader, 1) == '>')
      return tok_enddict;
  }

  if (c == '[')
    return tok_array;
  if (c == ']')
    return tok_endarray;

  if (c == '/')
    return tok_name;

  if (c == '(')
    return tok_string;

  if (c == '%')
    return tok_comment;

  if (is_keyword(reader, "obj"))
    return kw_obj;
  if (is_keyword(reader, "endobj"))
    return kw_endobj;
  if (is_keyword(reader, "stream"))
    return kw_stream;
  if (is_keyword(reader, "endstream"))
    return kw_endstream;
  if (is_keyword(reader, "R"))
    return kw_R;
  if (is_keyword(reader, "f"))
    return kw_f;
  if (is_keyword(reader, "n"))
    return kw_n;
  if (is_keyword(reader, "xref"))
    return kw_xref;
  if (is_keyword(reader, "trailer"))
    return kw_trailer;
  if (is_keyword(reader, "startxref"))
    return kw_startxref;
  if (is_keyword(reader, "true"))
    return kw_true;
  if (is_keyword(reader, "false"))
    return kw_false;
  if (is_keyword(reader, "null"))
    return kw_null;

  fatal("unknown token type %d (%c)\n", c, c);
}

static void advance_to_end_of_token(struct SpanReader* reader,
                                    struct Token* token) {
  switch (token->kind) {
    case kw_obj:
      reader_advance(reader, strlen("obj"));
      break;
    case kw_endobj:
      reader_advance(reader, strlen("endobj"));
      break;
    case kw_stream:
      reader_advance(reader, strlen("stream"));
      break;
    case kw_endstream:
      reader_advance(reader, strlen("endstream"));
      break;
    case kw_xref:
      reader_advance(reader, strlen("xref"));
      break;
    case kw_trailer:
      reader_advance(reader, strlen("trailer"));
      break;
    case kw_startxref:
      reader_advance(reader, strlen("startxref"));
      break;
    case kw_true:
      reader_advance(reader, strlen("true"));
      break;
    case kw_false:
      reader_advance(reader, strlen("false"));
      break;
    case kw_null:
      reader_advance(reader, strlen("null"));
      break;

    case tok_eof:
      break;

    case kw_R:
    case kw_f:
    case kw_n:
    case tok_array:
    case tok_endarray:
      reader_advance(reader, 1);
      break;

    case tok_name:
      reader_advance(reader, 1); // Skip '/'.
      while (!reader_empty(reader) && is_name_continuation(reader_cur(reader)))
        reader_advance(reader, 1);
      break;

    case tok_string:
      if (reader_cur(reader) == '(') {
        // 3.2.3 String Objects, Literal Strings
        reader_advance(reader, 1); // Skip '('.

        while (!reader_empty(reader)) {
          if (reader_cur(reader) == '\\') {
            reader_advance(reader, 1);
            if (reader_empty(reader))
              break;

            if (is_octal_digit(reader_cur(reader))) {
              if (reader_size(reader) < 3)
                fatal("incomplete octal escape\n");
              if (!is_octal_digit(reader_get(reader, 1)) ||
                  !is_octal_digit(reader_get(reader, 2)))
                fatal("invalid octal escape\n");
              reader_advance(reader, 3);
            } else {
              // Table 3.2 Escape sequences in literal strings
              switch(reader_cur(reader)) {
              case 'n':
              case 'r':
              case 't':
              case 'b':
              case 'f':
              case '(':
              case ')':
              case '\\':
                reader_advance(reader, 1);
              }
            }

            // "If the character following the backslash is not one of those
            //  shown in the table, the backslash is ignored."
            continue;
          }

          if (reader_cur(reader) == ')')
            break;

          reader_advance(reader, 1);
        }

        if (reader_empty(reader))
          fatal("unterminated () string\n");
        reader_advance(reader, 1); // Skip ')'.
      } else {
        // 3.2.3 String Objects, Hexadecimal Strings
        assert(reader_cur(reader) == '<');
        reader_advance(reader, 1); // Skip '<'.
        while (!reader_empty(reader) && reader_cur(reader) != '>')
          reader_advance(reader, 1);
        if (reader_empty(reader))
          fatal("unterminated <> string\n");
        reader_advance(reader, 1); // Skip '>'.
      }
      break;

    case tok_dict:
    case tok_enddict:
      reader_advance(reader, 2);
      break;

    case tok_comment:
      reader_advance(reader, 1); // Skip '%'.
      while (!reader_empty(reader) && !is_newline(reader_cur(reader)))
        reader_advance(reader, 1);
      if (!reader_empty(reader))
        consume_newline(reader);
      break;

    case tok_number:
      // Accepts `4.`, `-.02`, `0.0`, `1234`.
      reader_advance(reader, 1); // Skip number start.
      // FIXME: Accepts multiple periods too. Can that ever happen in valid
      //        PDF files?
      while (!reader_empty(reader) &&
             is_number_continuation(reader_cur(reader)))
        reader_advance(reader, 1);
      break;
  }
}

static void read_token(struct SpanReader* reader, struct Token* token) {
  advance_to_next_token(reader);
  token->payload.data = reader_data(reader);

  token->kind = classify_current(reader);

  advance_to_end_of_token(reader, token);
  token->payload.size = reader_data(reader) - token->payload.data;
}

static void dump_token(const struct Token* token) {
  switch (token->kind) {
  case kw_obj:
    printf("obj\n");
    break;
  case kw_endobj:
    printf("endobj\n");
    break;
  case kw_stream:
    printf("stream\n");
    break;
  case kw_endstream:
    printf("endstream\n");
    break;
  case kw_R:
    printf("R\n");
    break;
  case kw_f:
    printf("f\n");
    break;
  case kw_n:
    printf("n\n");
    break;
  case kw_xref:
    printf("xref\n");
    break;
  case kw_trailer:
    printf("trailer\n");
    break;
  case kw_startxref:
    printf("startxref\n");
    break;
  case kw_true:
    printf("true\n");
    break;
  case kw_false:
    printf("false\n");
    break;
  case kw_null:
    printf("null\n");
    break;

  case tok_number:
    printf("number '%.*s'\n", (int)token->payload.size, token->payload.data);
    break;

  case tok_dict:
    printf("<<\n");
    break;

  case tok_enddict:
    printf(">>\n");
    break;

  case tok_array:
    printf("[\n");
    break;

  case tok_endarray:
    printf("]\n");
    break;

  case tok_name:
    printf("name '%.*s'\n", (int)token->payload.size, token->payload.data);
    break;

  case tok_string:
    printf("string '%.*s'\n", (int)token->payload.size, token->payload.data);
    break;

  case tok_comment:
    printf("%%\n"); // XXX dump comment text?
    break;

  case tok_eof:
    printf("eof\n");
    break;
  }
}

static void dump_body(struct SpanReader* reader) {
  while (true) {
    struct Token token;
    read_token(reader, &token);

    //dump_token(&token);

    if (token.kind == tok_eof)
      break;

    // Ignore everything inside a `stream`.
    // FIXME: This is a bit janky. 3.7.1 Content Streams says
    //        "A content stream, after decoding with any specified filters,
    //         is interpreted according to the PDF syntax rules described in
    //         Section 3.1"
    //         I think this implies we have to make lexing context-sensitive
    //         for streams.
    // FIXME: Can streams contain `endstream` in their data? Do we have to
    //        use `Length` from the info dict?
    if (token.kind == kw_stream) {
      // 3.2.7 Stream Objects
      const uint8_t* e = memmem(reader_data(reader), reader_size(reader),
                                "endstream", strlen("endstream"));
      if (!e)
        fatal("missing `endstream`\n");
      reader_advance(reader, e - reader_data(reader));
    }
  }
}

static void dump(struct Span data) {
  // 3.4 File Structure
  struct SpanReader reader = { data, 0 };
  dump_header(&reader);
  dump_body(&reader);
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
