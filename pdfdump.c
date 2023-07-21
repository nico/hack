#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
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
(trailer and xref), and read only the objects needed for rendering the
currently visible page.  This here is for dumping all the data in a PDF, so it
starts at the start and reads all the data. Normally you wouldn't do that.

The file format is conceptually somewhat simple, but made harder by additions:
- Incremental Updates
- Linearization (1.2+)
- Object Streams (1.5+)
- Encryption
All of those aren't implemented yet.

To read a PDF, a regular viewer does:
- read the very first line, check that it's a PDF signature, and extracts
  the version number
- go to the end of the file, find `startxref` which has offset to `xref`,
  and `trailer`, which has number of objects (/Size),
  and id of root object (/Root)
- go to `xref`, which has offsets of all obj IDs
- using the `xref` table, read the root object and objs referenced from
  there to deserialize all data needed to render a given page
- if the trailer has an /Encrypt key, all string and stream contents need
  to be decrypted using the encryption method described by the object stored
  under that key before being processed further

For Incremental Updates (3.4.5 in the 1.7 spec), new data is written at the end
of the existing, unchanged PDF data. That means there's a second startxref (and
then a third, fourth, and so on, one more for each incremental safe) pointing
to a second `xref`, and a second trailer before the startxref.  The trailer
stores the offset of the previous xref (? XXX) (/Prev).  The xref tables are
read newest-to-oldest, and only offsets for objects that don't already have an
entry in a newer table are used from the older tables.

A linearized file stores a "linearization dictionary" at the top of the file,
right after the header, in the first 1024 bytes, containing only direct
objects. `/Linearized 1.0` marks the dictionary as linearization dictionary,
and that dictionary also stores file length (/L), primary hint stream offset
and length (/H), the id of the first page's page object (/O) and the offset to
the end of the first page (/E), the number of total pages in the document (/N)
and the offset to the first entry in the main xref (/T). After the
linearization dictionary is a first xref for just the first page, followed by a
first trailer that stores an offset to the main xref (/Prev), the total number
of xref entries (/Size), and the id of the root object (/Root). Then an
optional `startxref` with an ignored dummy offset (often `0`), followed by
`%%EOF`. Then comes the document catalog, then in any order both the first page
section (including objects for the first page and the outline hierarchy) and
the primary hint stream. After that, the remaining pages and their referenced
objects follow. Then some optional stuff, and finally the main xref, trailer,
startxref (pointing to the _first_ xref), %%EOF.  See Appendix F in
pdf_reference_1-7.pdf for details on linearized PDFs.

Object streams are documented in 3.4.6 Object Streams and
3.4.7 Cross-Reference Streams n the 1.7 spec. A Cross-Reference Stream stores
both `trailer` and `xref` data in an object stream: The `trailer` data in the
stream dict, the `xref` data in the stream's contents. It has `/Type /XRef`.
`startxref` now stores the offset of the cross-reference stream instead of
the `xref`. (Per Appendix H, Acrobat 6+ only supports /FlateDecode for
cross-reference streams.)

It's possible to have a hybrid file that has both traditional `xref` /
`trailer` and a cross-reference stream (for files readable by both 1.4- and
1.5+ readers). Here, startxref points at the traditional `xref` but the trailer
dictionary contains a `/XRefStm` key that points at the cross-reference object.
For reasons, this can only be used in update xref sections (see spec).

ideas:
- crunch
  - (re)compress streams
  - remove spaces between names
  - convert to object streams
  - remove remnants of incremental edits (multiple %%EOF, etc)
  - gc unused indirect objects
    - gc old unref'd generetations of indirect objects
  - gc unused direct objects (harder)
  - gc objects that have no visual/selection/a11y effect (even harder)
  - dedup multiple objects with identical contents (?)

- spy
  - show only hidden / unref'd contents

- compile
  - (re)compress streams
  - fill in binary comment at start (?)
  - fill in xref table / startxref offset
  - fill in stream lengths
  - renumber indirect objects (and refs to them), and update trailer /Size
  - check that indirect object ref indices make sense

- linearize / delinearize

- tool
  - pretty print (consistent newlines, indent dict contents, etc)
  - validate
    - everything parses (duh)
    - all xref entries point at indirect objs
    - xref entries have f / n set consistent with generation number
    - all indirect objs are ref'd by an xref table
    - startxref points at xref table
    - stream /Lengths consistent with contents / offset
    - hybrid file has consistent data in `xref` / `trailer` and in
      cross-reference stream
  - statistics
    - number of each object type
    - number of empty xref entries
    - number of indirect objects obsoleted by newer generation
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

static void print_usage(FILE* stream, const char* program_name) {
  fprintf(stream, "usage: %s [ options ] filename\n", program_name);
  fprintf(stream,
          "\n"
          "options:\n"
          "  --dump-tokens     dump output of tokenizer\n"
          "  --no-indent       disable auto-indentation of pretty-printer\n"
          "  --save-images     save images embedded in the PDF\n"
          "  --update-offsets  update offsets to match pretty-printed output\n"
          "  --quiet           print no output\n"
          "  -h  --help        print this message\n");
}

///////////////////////////////////////////////////////////////////////////////
// Lexer

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
  return c == 0 || c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ';
}

static bool is_newline(uint8_t c) {
  return c == '\n' || c == '\r';
}

static bool is_delimiter(uint8_t c) {
  // 3.1.1 Character Set
  return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' ||
         c == '{' || c == '}' || c == '/' || c == '%';
}

static bool is_digit(uint8_t c) {
  return c >= '0' && c <= '9';
}

static bool is_number_start(uint8_t c) {
  return c == '+' || c == '-' || c == '.' || is_digit(c);
}

static bool is_number_continuation(uint8_t c) {
  return c == '.' || is_digit(c);
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

static void advance_to_next_token(struct Span* data) {
  while (data->size && is_whitespace(data->data[0]))
    span_advance(data, 1);
}

static bool is_keyword(const struct Span* data, const char* keyword) {
  size_t n = strlen(keyword);
  if (data->size < n)
    return false;

  if (strncmp((char*)data->data, keyword, n))
    return false;

  return data->size == n || is_whitespace(data->data[n]) ||
         is_delimiter(data->data[n]);
}

static enum TokenKind classify_current(struct Span* data) {
  if (data->size == 0)
    return tok_eof;

  uint8_t c = data->data[0];
  assert(!is_whitespace(c));

  if (is_number_start(c))
    return tok_number;

  if (c == '<') {
    if (data->size > 1 && data->data[1] == '<')
      return tok_dict;
    return tok_string;
  }

  if (c == '>') {
    if (data->size > 1 && data->data[1] == '>')
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

  if (is_keyword(data, "obj"))
    return kw_obj;
  if (is_keyword(data, "endobj"))
    return kw_endobj;
  if (is_keyword(data, "stream"))
    return kw_stream;
  if (is_keyword(data, "endstream"))
    return kw_endstream;
  if (is_keyword(data, "R"))
    return kw_R;
  if (is_keyword(data, "f"))
    return kw_f;
  if (is_keyword(data, "n"))
    return kw_n;
  if (is_keyword(data, "xref"))
    return kw_xref;
  if (is_keyword(data, "trailer"))
    return kw_trailer;
  if (is_keyword(data, "startxref"))
    return kw_startxref;
  if (is_keyword(data, "true"))
    return kw_true;
  if (is_keyword(data, "false"))
    return kw_false;
  if (is_keyword(data, "null"))
    return kw_null;

  fatal("unknown token type %d (%c)\n", c, c);
}

static void advance_to_end_of_token(struct Span* data, struct Token* token) {
  switch (token->kind) {
    case kw_obj:
      span_advance(data, strlen("obj"));
      break;
    case kw_endobj:
      span_advance(data, strlen("endobj"));
      break;
    case kw_stream:
      span_advance(data, strlen("stream"));
      break;
    case kw_endstream:
      span_advance(data, strlen("endstream"));
      break;
    case kw_xref:
      span_advance(data, strlen("xref"));
      break;
    case kw_trailer:
      span_advance(data, strlen("trailer"));
      break;
    case kw_startxref:
      span_advance(data, strlen("startxref"));
      break;
    case kw_true:
      span_advance(data, strlen("true"));
      break;
    case kw_false:
      span_advance(data, strlen("false"));
      break;
    case kw_null:
      span_advance(data, strlen("null"));
      break;

    case tok_eof:
      break;

    case kw_R:
    case kw_f:
    case kw_n:
    case tok_array:
    case tok_endarray:
      span_advance(data, 1);
      break;

    case tok_name:
      span_advance(data, 1); // Skip '/'.
      while (data->size && is_name_continuation(data->data[0]))
        span_advance(data, 1);
      break;

    case tok_string:
      if (data->data[0] == '(') {
        // 3.2.3 String Objects, Literal Strings
        span_advance(data, 1); // Skip '('.

        while (data->size) {
          if (data->data[0] == '\\') {
            span_advance(data, 1);
            if (!data->size)
              break;

            if (is_octal_digit(data->data[0])) {
              if (data->size < 3)
                fatal("incomplete octal escape\n");
              if (!is_octal_digit(data->data[1]) ||
                  !is_octal_digit(data->data[2]))
                fatal("invalid octal escape\n");
              span_advance(data, 3);
            } else {
              // Table 3.2 Escape sequences in literal strings
              switch(data->data[0]) {
              case 'n':
              case 'r':
              case 't':
              case 'b':
              case 'f':
              case '(':
              case ')':
              case '\\':
                span_advance(data, 1);
              }
            }

            // "If the character following the backslash is not one of those
            //  shown in the table, the backslash is ignored."
            continue;
          }

          if (data->data[0] == ')')
            break;

          span_advance(data, 1);
        }

        if (!data->size)
          fatal("unterminated () string\n");
        span_advance(data, 1); // Skip ')'.
      } else {
        // 3.2.3 String Objects, Hexadecimal Strings
        assert(data->data[0] == '<');
        span_advance(data, 1); // Skip '<'.
        while (data->size && data->data[0] != '>')
          span_advance(data, 1);
        if (!data->size)
          fatal("unterminated <> string\n");
        span_advance(data, 1); // Skip '>'.
      }
      break;

    case tok_dict:
    case tok_enddict:
      span_advance(data, 2);
      break;

    case tok_comment:
      span_advance(data, 1); // Skip '%'.
      while (data->size && !is_newline(data->data[0]))
        span_advance(data, 1);
      // Make newline after the comment not part of the comment's payload.
      break;

    case tok_number:
      // Accepts `4.`, `-.02`, `0.0`, `1234`.
      span_advance(data, 1); // Skip number start.
      // FIXME: Accepts multiple periods too. Can that ever happen in valid
      //        PDF files?
      while (data->size && is_number_continuation(data->data[0]))
        span_advance(data, 1);
      break;
  }
}

static void read_token_at_current_position(struct Span* data, struct Token* token) {
  token->payload.data = data->data;

  token->kind = classify_current(data);

  advance_to_end_of_token(data, token);
  token->payload.size = data->data - token->payload.data;
}

static void read_token(struct Span* data, struct Token* token) {
  advance_to_next_token(data);
  read_token_at_current_position(data, token);
}

static void read_non_eof_token(struct Span* data, struct Token* token) {
  read_token(data, token);
  if (token->kind == tok_eof)
    fatal("unexpected eof\n");
}

static void read_non_eof_token_at_current_position(struct Span* data,
                                                   struct Token* token) {
  read_token_at_current_position(data, token);
  if (token->kind == tok_eof)
    fatal("unexpected eof\n");
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

static void dump_tokens(struct Span* data) {
  while (true) {
    struct Token token;
    read_token(data, &token);
    dump_token(&token);

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
      const uint8_t* e = memmem(data->data, data->size,
                                "endstream", strlen("endstream"));
      if (!e)
        fatal("missing `endstream`\n");
      span_advance(data, e - data->data);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// AST

// 3.2 Objects
enum ObjectKind {
  Boolean,
  Integer,
  Real,
  String,  // (hi) or <6869>
  Name,    // /hi
  Array,  // [0 1 2]
  Dictionary,  //  <</Name1 (val1) /Name2 (val2)>>

  // 3.2.7 Stream Objects
  // "All streams must be indirect objects and the stream dictionary must
  //  be a direct object."
  Stream,

  Null,
  IndirectObject,  // 4 0 obj ... endobj
  IndirectObjectRef,  // 4 0 R

  // FIXME: Having an AST node for comments is a bit weird: where should it go
  //        for dicts? Where if the comment is between the indirect obj or
  //        indirect obj ref tokens?
  Comment, // For pretty-printing

  // In traditional PDFs, the `xref` table is at the end of the file.
  // But for incremental updates, and for linearized PDFs, the xref table
  // can appear multiple times, sometimes in the middle of the file; or it might
  // be hidden away in an object stream, and only `startxref` might be visible.
  // So represent `xref`, `startxref`, and `trailer` as normal objects.
  XRef,
  Trailer,
  StartXRef,
};

struct Object {
  enum ObjectKind kind;

  // Array-of-Structures design; pick array to index into based on `kind`.
  size_t index;
};

struct BooleanObject {
  bool value;
};

struct IntegerObject {
  int32_t value;
};

struct RealObject {
  double value;
};

struct StringObject {
  struct Span value;
};

struct NameObject {
  struct Span value;
};

struct ArrayObject {
  size_t count;
  struct Object* elements;
};

struct NameObjectPair {
  struct NameObject name;
  struct Object value;
};

struct DictionaryObject {
  size_t count;
  struct NameObjectPair* elements;
};

struct Object* dict_get(const struct DictionaryObject* dict, const char* key) {
  // FIXME: maybe have a lazily-created hash table on the side?
  size_t l = strlen(key);
  for (size_t i = 0; i < dict->count; ++i) {
    if (dict->elements[i].name.value.size != l ||
        strncmp(key, (char*)dict->elements[i].name.value.data, l))
      continue;

    // 3.2.6 Dictionary Objects
    // "A dictionary entry whose value is `null` is equivalent ot an absent entry."
    if (dict->elements[i].value.kind == Null)
      return NULL;
    return &dict->elements[i].value;
  }
  return NULL;
}

void dict_append(struct DictionaryObject* dict,
                 struct NameObjectPair new_element) {
  // new_element.key must not yet be in the dict.
  dict->elements = realloc(dict->elements,
                           (dict->count + 1) * sizeof(struct NameObjectPair));
  dict->elements[dict->count] = new_element;
  dict->count++;
}

struct StreamObject {
  struct DictionaryObject dict;
  struct Span data;
};

struct IndirectObjectObject {
  size_t id;
  size_t generation;
  struct Object value;
};

struct IndirectObjectRefObject {
  size_t id;
  size_t generation;
};

struct CommentObject {
  struct Span value;
};

struct XRefEntry {
  size_t offset;
  size_t generation;
  bool is_free; // true for 'f', false for 'n'
};

struct XRefObject {
  size_t start_id;
  size_t count;
  struct XRefEntry* entries;
};

struct TrailerObject {
  struct DictionaryObject dict;
};

struct StartXRefObject {
  size_t offset;
};

struct PDFVersion {
  uint8_t major;
  uint8_t minor;
};

struct PDF {
  struct PDFVersion version;

  // AST storage.
  // Stores all objects of a type, including nested direct objects.
  struct BooleanObject* booleans;
  size_t booleans_count;
  size_t booleans_capacity;

  struct IntegerObject* integers;
  size_t integers_count;
  size_t integers_capacity;

  struct RealObject* reals;
  size_t reals_count;
  size_t reals_capacity;

  struct StringObject* strings;
  size_t strings_count;
  size_t strings_capacity;

  struct NameObject* names;
  size_t names_count;
  size_t names_capacity;

  struct ArrayObject* arrays;
  size_t arrays_count;
  size_t arrays_capacity;

  struct DictionaryObject* dicts;
  size_t dicts_count;
  size_t dicts_capacity;

  struct StreamObject* streams;
  size_t streams_count;
  size_t streams_capacity;

  struct IndirectObjectObject* indirect_objects;
  size_t indirect_objects_count;
  size_t indirect_objects_capacity;

  struct IndirectObjectRefObject* indirect_object_refs;
  size_t indirect_object_refs_count;
  size_t indirect_object_refs_capacity;

  struct CommentObject* comments;
  size_t comments_count;
  size_t comments_capacity;

  struct XRefObject* xrefs;
  size_t xrefs_count;
  size_t xrefs_capacity;

  struct TrailerObject* trailers;
  size_t trailers_count;
  size_t trailers_capacity;

  struct StartXRefObject* start_xrefs;
  size_t start_xrefs_count;
  size_t start_xrefs_capacity;

  // Only stores toplevel objects.
  // The indices in these Objects refer to the arrays above.
  struct Object* toplevel_objects;
  size_t toplevel_objects_count;
  size_t toplevel_objects_capacity;

  // Parsing option.
  // FIXME: Should probably be somewhere else.
  bool trust_stream_lengths;
};

static void init_pdf(struct PDF* pdf) {
#define N 5
#define INIT(name, type) \
  pdf->name = malloc(N * sizeof(struct type)); \
  pdf->name##_count = 0; \
  pdf->name##_capacity = N

  INIT(booleans, BooleanObject);
  INIT(integers, IntegerObject);
  INIT(reals, RealObject);
  INIT(strings, StringObject);
  INIT(names, NameObject);
  INIT(arrays, ArrayObject);
  INIT(dicts, DictionaryObject);
  INIT(streams, StreamObject);
  INIT(indirect_objects, IndirectObjectObject);
  INIT(indirect_object_refs, IndirectObjectRefObject);
  INIT(comments, CommentObject);
  INIT(xrefs, XRefObject);
  INIT(trailers, TrailerObject);
  INIT(start_xrefs, StartXRefObject);

  INIT(toplevel_objects, Object);
#undef INIT
#undef N

  pdf->trust_stream_lengths = true;
}

static void append_boolean(struct PDF* pdf, struct BooleanObject value) {
  if (pdf->booleans_count >= pdf->booleans_capacity) {
    pdf->booleans_capacity *= 2;
    pdf->booleans = realloc(pdf->booleans,
                            pdf->booleans_capacity * sizeof(struct BooleanObject));
  }
  pdf->booleans[pdf->booleans_count++] = value;
}

static void append_integer(struct PDF* pdf, struct IntegerObject value) {
  if (pdf->integers_count >= pdf->integers_capacity) {
    pdf->integers_capacity *= 2;
    pdf->integers = realloc(pdf->integers,
                            pdf->integers_capacity * sizeof(struct IntegerObject));
  }
  pdf->integers[pdf->integers_count++] = value;
}

static void append_real(struct PDF* pdf, struct RealObject value) {
  if (pdf->reals_count >= pdf->reals_capacity) {
    pdf->reals_capacity *= 2;
    pdf->reals = realloc(pdf->reals,
                         pdf->reals_capacity * sizeof(struct RealObject));
  }
  pdf->reals[pdf->reals_count++] = value;
}

static void append_name(struct PDF* pdf, struct NameObject value) {
  if (pdf->names_count >= pdf->names_capacity) {
    pdf->names_capacity *= 2;
    pdf->names = realloc(pdf->names,
                         pdf->names_capacity * sizeof(struct NameObject));
  }
  pdf->names[pdf->names_count++] = value;
}

static void append_string(struct PDF* pdf, struct StringObject value) {
  if (pdf->strings_count >= pdf->strings_capacity) {
    pdf->strings_capacity *= 2;
    pdf->strings = realloc(pdf->strings,
                           pdf->strings_capacity * sizeof(struct StringObject));
  }
  pdf->strings[pdf->strings_count++] = value;
}

static void append_array(struct PDF* pdf, struct ArrayObject value) {
  if (pdf->arrays_count >= pdf->arrays_capacity) {
    pdf->arrays_capacity *= 2;
    pdf->arrays = realloc(pdf->arrays,
                          pdf->arrays_capacity * sizeof(struct ArrayObject));
  }
  pdf->arrays[pdf->arrays_count++] = value;
}

static void append_dict(struct PDF* pdf, struct DictionaryObject value) {
  if (pdf->dicts_count >= pdf->dicts_capacity) {
    pdf->dicts_capacity *= 2;
    pdf->dicts = realloc(pdf->dicts,
                         pdf->dicts_capacity * sizeof(struct DictionaryObject));
  }
  pdf->dicts[pdf->dicts_count++] = value;
}

static void append_stream(struct PDF* pdf, struct StreamObject value) {
  if (pdf->streams_count >= pdf->streams_capacity) {
    pdf->streams_capacity *= 2;
    pdf->streams = realloc(pdf->streams,
                           pdf->streams_capacity * sizeof(struct StreamObject));
  }
  pdf->streams[pdf->streams_count++] = value;
}

static void append_indirect_object(struct PDF* pdf,
                                   struct IndirectObjectObject value) {
  if (pdf->indirect_objects_count >= pdf->indirect_objects_capacity) {
    pdf->indirect_objects_capacity *= 2;
    pdf->indirect_objects = realloc(
      pdf->indirect_objects,
      pdf->indirect_objects_capacity * sizeof(struct IndirectObjectObject));
  }
  pdf->indirect_objects[pdf->indirect_objects_count++] = value;
}

static void append_indirect_object_ref(struct PDF* pdf,
                                       struct IndirectObjectRefObject value) {
  if (pdf->indirect_object_refs_count >= pdf->indirect_object_refs_capacity) {
    pdf->indirect_object_refs_capacity *= 2;
    pdf->indirect_object_refs = realloc(
      pdf->indirect_object_refs,
      pdf->indirect_object_refs_capacity * sizeof(struct IndirectObjectRefObject));
  }
  pdf->indirect_object_refs[pdf->indirect_object_refs_count++] = value;
}

static void append_comment(struct PDF* pdf, struct CommentObject value) {
  if (pdf->comments_count >= pdf->comments_capacity) {
    pdf->comments_capacity *= 2;
    pdf->comments = realloc(pdf->comments,
                            pdf->comments_capacity * sizeof(struct CommentObject));
  }
  pdf->comments[pdf->comments_count++] = value;
}

static void append_xref(struct PDF* pdf, struct XRefObject value) {
  if (pdf->xrefs_count >= pdf->xrefs_capacity) {
    pdf->xrefs_capacity *= 2;
    pdf->xrefs = realloc(pdf->xrefs,
                         pdf->xrefs_capacity * sizeof(struct XRefObject));
  }
  pdf->xrefs[pdf->xrefs_count++] = value;
}

static void append_trailer(struct PDF* pdf, struct TrailerObject value) {
  if (pdf->trailers_count >= pdf->trailers_capacity) {
    pdf->trailers_capacity *= 2;
    pdf->trailers = realloc(pdf->trailers,
                            pdf->trailers_capacity * sizeof(struct TrailerObject));
  }
  pdf->trailers[pdf->trailers_count++] = value;
}

static void append_start_xref(struct PDF* pdf, struct StartXRefObject value) {
  if (pdf->start_xrefs_count >= pdf->start_xrefs_capacity) {
    pdf->start_xrefs_capacity *= 2;
    pdf->start_xrefs = realloc(
      pdf->start_xrefs,
      pdf->start_xrefs_capacity * sizeof(struct StartXRefObject));
  }
  pdf->start_xrefs[pdf->start_xrefs_count++] = value;
}

static void append_toplevel_object(struct PDF* pdf, struct Object value) {
  if (pdf->toplevel_objects_count >= pdf->toplevel_objects_capacity) {
    pdf->toplevel_objects_capacity *= 2;
    pdf->toplevel_objects = realloc(
      pdf->toplevel_objects,
      pdf->toplevel_objects_capacity * sizeof(struct Object));
  }
  pdf->toplevel_objects[pdf->toplevel_objects_count++] = value;
}

///////////////////////////////////////////////////////////////////////////////
// Parser

static void parse_header(struct Span* data, struct PDFVersion* version) {
  if (data->size < strlen("%PDF-a.b\n"))
    fatal("not enough data for header\n");

  if (strncmp("%PDF-", (char*)data->data, strlen("%PDF-")))
    fatal("wrong file start: no %%PDF-\n");

  if (data->data[strlen("%PDF-a")] != '.')
    fatal("wrong file start: no dot?\n");

  uint8_t v0 = data->data[strlen("%PDF-")] - '0';
  uint8_t v1 = data->data[strlen("%PDF-a.")] - '0';

  span_advance(data, strlen("%PDF-a.b"));

  // `tiff2pdf` output includes an extra space. Skip it.
  if (data->size && data->data[0] == ' ')
    span_advance(data, 1);

  consume_newline(data);

  version->major = v0;
  version->minor = v1;
}

static struct Object parse_object(struct PDF* pdf, struct Span* data, struct Token);

static struct Object parse_indirect_object(struct PDF* pdf, struct Span* data) {
  struct Token token;

  read_non_eof_token(data, &token);
  struct Object object = parse_object(pdf, data, token);

  read_non_eof_token(data, &token);
  if (token.kind != kw_endobj)
    fatal("expected `endobj`\n");

  return object;
}

static struct StreamObject parse_stream(struct PDF* pdf, struct Span* data,
                                        struct DictionaryObject stream_dict) {
  (void)pdf;

  // 3.2.7 Stream Objects
  // "The keyword `stream` that follows the stream dictionary should be followed by
  //  and end-of-line marker consisting of either a carriage return and a line feed
  //  or just a line feed, and not by a carriage return alone. [...]
  //  It is recommended that there be an end-of-line marker after the data and
  //  before `endstream`; this marker is not included in the stream length."
  if (data->size && data->data[0] == '\n')
    span_advance(data, 1);
  else if (data->size >= 2 && data->data[0] == '\r' && data->data[1] == '\n')
    span_advance(data, 2);
  uint8_t* data_start = data->data;

  // Ignore everything inside a `stream`.
  // Use /Length from stream_dict if present.
  // ...but see example 3.1, /Length could be an indirect object that's
  // defined only later in the file. In that case, need to scan too.
  // FIXME: Parse `startxref` and `xref` early and get offset of indirect
  //        length object from there instead in that case, like a regular
  //        PDF viewer would do. (This won't work with update_offsets where
  //        we're supposed to fill in /Length.)
  struct Object* length_object = dict_get(&stream_dict, "/Length");

  if (length_object && length_object->kind != IndirectObjectRef &&
      length_object->kind != Integer)
    fatal("stream /Length unexpectedly neither indirect obj ref nor integer\n");

  size_t data_size;
  if (!pdf->trust_stream_lengths || !length_object ||
      length_object->kind == IndirectObjectRef) {
    const uint8_t* e = memmem(data->data, data->size,
                              "endstream", strlen("endstream"));
    if (!e)
      fatal("missing `endstream`\n");
    span_advance(data, e - data->data);

    // Skip newline in front of `endline` if present.
    if (e > data_start && e[-1] == '\r') {
      --e;
    } else if (e > data_start && e[-1] == '\n') {
      --e;
      if (e > data_start && e[-1] == '\r')
        --e;
    }
    data_size = e - data_start;
  } else {
    if (length_object->kind != Integer)
      fatal("stream /Length unexpectedly not indirect object and not integer\n");
    data_size = pdf->integers[length_object->index].value;
    span_advance(data, data_size);

    if (data->size && is_newline(data->data[0]))
      consume_newline(data);
  }

  // This must not skip whitespace; `endstream` must be at the start of `data` here,
  // else things are awry.
  if (!data->size || is_whitespace(data->data[0]))
    fatal("unexpected data after stream");
  struct Token token;
  read_non_eof_token_at_current_position(data, &token);

  // This can fail if we get the length from /Length. In the fallback case, it's
  // always true.
  if (token.kind != kw_endstream)
    fatal("missing `endstream` after stream");

  struct Span stream_data = { data_start, data_size };

  struct StreamObject stream;
  stream.dict = stream_dict;
  stream.data = stream_data;
  return stream;
}

static struct DictionaryObject parse_dict(struct PDF* pdf, struct Span* data) {
#define N 1000 // FIXME: jank; good enough for now
  struct NameObjectPair entries[N];

  size_t i = 0;
  while (true) {
    struct Token token;
    read_non_eof_token(data, &token);

    if (token.kind == tok_enddict)
      break;

    if (i >= N)
      fatal("XXX too many dict entries\n"); // XXX

    if (token.kind != tok_name)
      fatal("expected name\n");

    entries[i].name.value = token.payload;

    read_non_eof_token(data, &token);
    entries[i].value = parse_object(pdf, data, token);

    i++;
  }

  // FIXME: Store entries inline, or at least in a bumpptr allocator?
  struct DictionaryObject dict;
  dict.count = i;
  dict.elements = malloc(i * sizeof(struct NameObjectPair));
  memcpy(dict.elements, entries, i * sizeof(struct NameObjectPair));
  return dict;
#undef N
}

static struct ArrayObject parse_array(struct PDF* pdf, struct Span* data) {
#define N 10000 // FIXME: jank; good enough for now
  struct Object entries[N];

  size_t i = 0;
  while (true) {
    struct Token token;
    read_non_eof_token(data, &token);

    if (token.kind == tok_endarray)
      break;

    if (i >= N)
      fatal("XXX too many array entries\n"); // XXX

    // FIXME: This gets confused about comments within a dict.

    entries[i++] = parse_object(pdf, data, token);
  }

  // FIXME: Store entries inline, or at least in a bumpptr allocator?
  struct ArrayObject array;
  array.count = i;
  array.elements = malloc(i * sizeof(struct Object));
  memcpy(array.elements, entries, i * sizeof(struct Object));
  return array;
#undef N
}

static bool is_integer_token(const struct Token* token) {
  if (token->kind != tok_number)
    return false;
  return !memchr(token->payload.data, '.', token->payload.size);
}

static int32_t integer_value(const struct Token* token) {
  if (!is_integer_token(token))
    fatal("expected integer\n");

  // This works even though `payload` isn't nul-terminated because strtol()
  // stops on the first invalid character, and the lexer guarantees that
  // exactly the chars within `payload` are valid.
  char* endptr = NULL;
  int32_t value = (int32_t)strtol((char*)token->payload.data, &endptr, 10);
  if (endptr != (char*)token->payload.data + token->payload.size)
    fatal("invalid integer number\n");

  return value;
}

static double real_value(const struct Token* token) {
  if (token->kind != tok_number)
    fatal("expected number\n");

  // This works even though `payload` isn't nul-terminated because strtod()
  // stops on the first invalid character, and the lexer guarantees that
  // exactly the chars within `payload` are valid.
  char* endptr = NULL;
  double value = strtod((char*)token->payload.data, &endptr);
  if (endptr != (char*)token->payload.data + token->payload.size)
    fatal("invalid real number\n");

  return value;
}

static struct XRefObject parse_xref(struct PDF* pdf, struct Span* data) {
  (void)pdf;

  struct Token token;
  read_non_eof_token(data, &token);
  int32_t start_id = integer_value(&token); // XXX check >= 0

  read_non_eof_token(data, &token);
  int32_t count = integer_value(&token); // XXX check >= 0

  while (data->size && is_whitespace(data->data[0]))
    span_advance(data, 1);

  if (data->size < 20 * (unsigned)count)
    fatal("not enough data for xref entries\n");

  // Per 3.4.3 Cross-Reference Table:
  // "Following this line are one or more cross-reference subsections"
  // FIXME: this parses only a single cross-reference subsection at the moment.

  struct XRefEntry* entries = malloc(count * sizeof(struct XRefEntry));

  for (int i = 0; i < count; ++i) {
    struct XRefEntry entry;

    char* end;
    entry.offset = strtol((char*)data->data, &end, 10);
    if (end != (char*)data->data + 10)
      fatal("failed to parse xref offset\n");

    if (data->data[10] != ' ')
      fatal("xref missing first space\n");

    entry.generation = strtol((char*)data->data + 11, &end, 10);
    if (end != (char*)data->data + 16)
      fatal("failed to parse xref generation\n");

    if (data->data[16] != ' ')
      fatal("xref missing second space\n");

    char flag = data->data[17];
    if (flag != 'f' && flag != 'n')
      fatal("unexpected xref flag\n");
    entry.is_free = flag == 'f';

    // 3.4.3 Cross-Reference Table:
    // "If the file's end-of-line marker is a single character ([...]), it is
    //  preceded by a single space; if the marker is 2 characters (both a carriage
    //  return and a line feed), it is not preceded by a space."
    if ((!is_newline(data->data[18]) && data->data[18] != ' ') ||
        !is_newline(data->data[19]))
      fatal("xref bad newline\n");

    span_advance(data, 20);
    entries[i] = entry;
  }

  // FIXME: Store entries inline, or at least in a bumpptr allocator?
  struct XRefObject xref;
  xref.start_id = start_id;
  xref.count = count;
  xref.entries = entries;
  return xref;
#undef N
}


static struct Object parse_object(struct PDF* pdf, struct Span* data,
                                  struct Token token) {
  switch (token.kind) {
  case kw_obj:
    fatal("unexpected `obj`\n");
  case kw_endobj:
    fatal("unexpected `endobj`\n");

  case kw_stream:
    fatal("unexpected `stream`\n");
  case kw_endstream:
    fatal("unexpected `endstream`\n");

  case kw_R:
    fatal("unexpected `R`\n");
  case kw_f:
    fatal("unexpected `f`\n");
  case kw_n:
    fatal("unexpected `n`\n");
  case kw_xref:
    append_xref(pdf, parse_xref(pdf, data));
    return (struct Object){ XRef, pdf->xrefs_count - 1 };
  case kw_trailer: {
    read_token(data, &token);
    if (token.kind != tok_dict)
      fatal("expected << after trailer");
    struct DictionaryObject dict = parse_dict(pdf, data);
    append_trailer(pdf, (struct TrailerObject){ dict });
    return (struct Object){ Trailer, pdf->trailers_count - 1 };
  }
  case kw_startxref: {
    // FIXME: only allow at toplevel
    // FIXME: startxrefs probably isn't limited to INT_MAX.
    read_token(data, &token);
    int32_t offset = integer_value(&token);
    append_start_xref(pdf, (struct StartXRefObject){ offset });
    return (struct Object){ StartXRef, pdf->start_xrefs_count - 1 };
  }

  case kw_true:
    append_boolean(pdf, (struct BooleanObject){ true });
    return (struct Object){ Boolean, pdf->booleans_count - 1 };
  case kw_false:
    append_boolean(pdf, (struct BooleanObject){ false });
    return (struct Object){ Boolean, pdf->booleans_count - 1 };
  case kw_null:
    return (struct Object){ Null, -1 };

  case tok_number: {
    if (is_integer_token(&token)) {
      int32_t value = integer_value(&token);

      // Peek two tokens in the future, make an IndirectObjectRef if 'num' 'R',
      // or an IndirectObject if 'num' 'obj'.
      // FIXME: This gets confused about comments between tokens.
      struct Span backtrack_position = *data;
      read_token(data, &token);

      if (is_integer_token(&token)) {
        int32_t generation = integer_value(&token);

        // FIXME: does PDF allow non-indirect obj at toplevel, and does it allow
        //        indirect objs at non-toplevel?
        read_token(data, &token);
        if (token.kind == kw_obj) {
          struct Object object = parse_indirect_object(pdf, data);
          append_indirect_object(
            pdf, (struct IndirectObjectObject){ value, generation, object });
          return (struct Object){ IndirectObject,
                                  pdf->indirect_objects_count - 1 };
        } else if (token.kind == kw_R) {
          append_indirect_object_ref(
            pdf, (struct IndirectObjectRefObject){ value, generation });
          return (struct Object){ IndirectObjectRef,
                                  pdf->indirect_object_refs_count - 1 };
        }
      }
      *data = backtrack_position;

      append_integer(pdf, (struct IntegerObject){ value });
      return (struct Object){ Integer, pdf->integers_count - 1 };
    }

    append_real(pdf, (struct RealObject){ real_value(&token) });
    return (struct Object){ Real, pdf->reals_count - 1 };
  }

  case tok_dict: {
    struct DictionaryObject dict = parse_dict(pdf, data);
    // Check if followed by `stream`, if so parse stream body and add stream,
    // else add dict.
    struct Span backtrack_position = *data;

    // FIXME: This gets confused about comments before `stream`.
    read_token(data, &token);
    if (token.kind == kw_stream) {
      // FIXME: only allow at toplevel
      append_stream(pdf, parse_stream(pdf, data, dict));
      return (struct Object){ Stream, pdf->streams_count - 1 };
    }

    *data = backtrack_position;

    append_dict(pdf, dict);
    return (struct Object){ Dictionary, pdf->dicts_count - 1 };
  }

  case tok_enddict:
    fatal("unexpected `enddict`\n");

  case tok_array:
    append_array(pdf, parse_array(pdf, data));
    return (struct Object){ Array, pdf->arrays_count - 1 };

  case tok_endarray:
    fatal("unexpected `]`\n");

  case tok_name:
    append_name(pdf, (struct NameObject){ token.payload });
    return (struct Object){ Name, pdf->names_count - 1 };

  case tok_string:
    append_string(pdf, (struct StringObject){ token.payload });
    return (struct Object){ String, pdf->strings_count - 1 };

  case tok_comment:
    append_comment(pdf, (struct CommentObject){ token.payload });
    return (struct Object){ Comment, pdf->comments_count - 1 };

  case tok_eof:
    fatal("premature EOF\n");
  }
}

static void parse_pdf(struct Span data, struct PDF* pdf) {
  // 3.4 File Structure
  // 1. Header
  // %PDF-1.0
  parse_header(&data, &pdf->version);

  // %nonasciichars

  // 2. Body
  // `objnum` gennum obj
  //   (any pdfobject)
  // `endobj`

  // 3. Cross-reference table
  // `xref`
  // 0 n
  // 0000000000 65535 f
  // 0000000015 00000 `n`   n times (offset, size, `n`)

  // 4. Trailer
  // `trailer`

  // 5. %%EOF

  struct Token token;
  while (true) {
    read_token(&data, &token);

    if (token.kind == tok_eof)
      break;

    struct Object object = parse_object(pdf, &data, token);

    // FIXME: is this true?
    if (object.kind != IndirectObject && object.kind != Comment &&
        object.kind != XRef && object.kind != Trailer && object.kind != StartXRef) {
      fatal("expected indirect object or comment or xref or trailer or startxref "
            "at toplevel\n");
    }

    // FIXME: maybe not even necessary to store all the toplevels;
    // instead just process them as they come in?
    // (Doesn't work with the current strategy of xref offset updating, so would
    // have to tweak that.)
    append_toplevel_object(pdf, object);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Pretty-printer

enum StreamOptions {
  PrintRawData,
  PrintRawDataUnfiltered,
  PrintSummary,
};

struct OutputOptions {
  bool indent_output;
  bool save_images;
  bool update_offsets;
  bool quiet;

  unsigned current_indent;
  bool is_on_start_of_line;

  size_t bytes_written;
  size_t xref_offset;

  size_t* indirect_object_offsets;

  enum StreamOptions stream_options;
};

static void init_output_options(struct OutputOptions* options) {
  options->indent_output = true;
  options->save_images = false;
  options->update_offsets = false;
  options->quiet = false;

  options->current_indent = 0;
  options->is_on_start_of_line = true;
  options->bytes_written = 0;

  options->xref_offset = 0;
  options->indirect_object_offsets = NULL;

  options->stream_options = PrintRawData;
}

static void increase_indent(struct OutputOptions* options) {
  options->current_indent += 2;
}

static void decrease_indent(struct OutputOptions* options) {
  options->current_indent -= 2;
}

static int print_indent(const struct OutputOptions* options) {
  for (unsigned i = 0; i < options->current_indent; ++i)
    printf(" ");
  return options->current_indent;
}

#if defined(__clang__) || defined(__GNUC__)
#define PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define PRINTF(a, b)
#endif

static void nvprintf(struct OutputOptions* options, const char* msg, va_list args) {
  int vprintf_result = vprintf(msg, args);

  if (vprintf_result < 0)
    fatal("error writing output");

  options->is_on_start_of_line = false;
  options->bytes_written += vprintf_result;
}

PRINTF(2, 3)
static void nprintf(struct OutputOptions* options, const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  nvprintf(options, msg, args);
  va_end(args);
}

PRINTF(2, 3)
static void iprintf(struct OutputOptions* options, const char* msg, ...) {
  if (options->is_on_start_of_line) {
    if (options->indent_output)
      options->bytes_written += print_indent(options);
  }

  va_list args;
  va_start(args, msg);
  nvprintf(options, msg, args);
  va_end(args);

  if (msg[strlen(msg) - 1] == '\n')
    options->is_on_start_of_line = true;
}

static void iprint_binary_newline(struct OutputOptions* options,
                                  const struct Span* data) {
  if (options->is_on_start_of_line) {
    if (options->indent_output)
      options->bytes_written += print_indent(options);
  }

  size_t n = fwrite(data->data, 1, data->size, stdout);
  if (n != data->size)
    fatal("failed to write string data\n");
  options->bytes_written += data->size;

  if (fputc('\n', stdout) == EOF)
    fatal("failed to write newline\n");
  options->bytes_written += 1;

  options->is_on_start_of_line = true;
}

static void ast_print(struct OutputOptions*, struct PDF* pdf,
                      const struct Object* object);

static void ast_print_name(struct OutputOptions* options,
                           const struct NameObject* name, bool print_newline) {
  iprintf(options, "%.*s", (int)name->value.size, name->value.data);
  if (print_newline)
    iprintf(options, "\n");
}

static void ast_print_dict(struct OutputOptions* options, struct PDF* pdf,
                           const struct DictionaryObject* dict) {
  iprintf(options, "<<\n");
  increase_indent(options);
  for (size_t i = 0; i < dict->count; ++i) {
    ast_print_name(options, &dict->elements[i].name, /*print_newline=*/false);
    nprintf(options, " ");
    ast_print(options, pdf, &dict->elements[i].value);
  }
  decrease_indent(options);
  iprintf(options, ">>\n");
}

static struct Span uncompress_flate(struct Span data) {
    void* zlib = dlopen("libz.so", RTLD_LAZY);
    if (!zlib)
      zlib = dlopen("libz.dylib", RTLD_LAZY);
    if (!zlib)
      fatal("failed to load libz\n");

    typedef int (*Uncompress)(uint8_t*, unsigned long*, const uint8_t*, unsigned long);
    Uncompress uncompress = (Uncompress)dlsym(zlib, "uncompress");
    if (!uncompress)
      fatal("no uncompress() in zlib?\n");

    unsigned long uncompressed_size, guess = data.size;
    void* inflated = NULL;

    int err;
    do {
      uncompressed_size = guess;
      inflated = realloc(inflated, uncompressed_size);

      err = uncompress(inflated, &uncompressed_size, data.data, data.size);
      guess *= 2;
    } while (err == -5 /* == Z_BUF_ERROR*/);

    if (err != 0 /* == Z_OK */)
      fatal("failed to uncompress %d\n", err);

    return (struct Span){ inflated, uncompressed_size };
}

static void update_stream_length(struct PDF* pdf, struct StreamObject* stream) {
  struct Object* length_object = dict_get(&stream->dict, "/Length");
  if (!length_object) {
    append_integer(pdf, (struct IntegerObject){ stream->data.size });
    struct Object size = { Integer, pdf->integers_count - 1 };
    struct Span length_span = { (uint8_t*)"/Length", strlen("/Length") };
    struct NameObject length_name = { length_span };
    dict_append(&stream->dict, (struct NameObjectPair){ length_name, size });
    return;
  }

  if (length_object->kind == IndirectObject) {
    // FIXME
    // https://www.w3.org/WAI/ER/tests/xhtml/testfiles/resources/pdf/dummy.pdf
    // is an example of a file that hits this.
    fatal("cannot update indirect stream /Length yet");
  }

  assert(length_object->kind == Integer);

  pdf->integers[length_object->index].value = stream->data.size;
}

static void ast_print_stream(struct OutputOptions* options, struct PDF* pdf,
                             struct StreamObject* stream) {
  if (options->update_offsets)
    update_stream_length(pdf, stream);

  increase_indent(options);
  ast_print_dict(options, pdf, &stream->dict);
  decrease_indent(options);
  iprintf(options, "stream\n");
  // FIXME: optionally re-filter contents

  struct Span stream_data = stream->data;

  switch (options->stream_options) {
  case PrintRawDataUnfiltered: {
    struct Object* filter = dict_get(&stream->dict, "/Filter");
    if (filter) {
      if (filter->kind == Array) {
        fprintf(stderr, "warning: cannot uncompress /Filter with array yet\n");
        goto fallthrough;
      }
      if (filter->kind != Name)
        fatal("unexpected /Filter type\n");
      struct NameObject* name = &pdf->names[filter->index];
      if (!strncmp((char*)name->value.data, "/FlateDecode", name->value.size)) {
        // FIXME: This doesn't work for encrypted files.
        //        Encryption modifies stream data but doesn't update /Length.
        // FIXME: check /DecodeParms
        // FIXME: Don't write `/Filter /FlateDecode` to output when the data
        //        is uncompressed!
        stream_data = uncompress_flate(stream_data);
      } else {
        fprintf(stderr, "warning: unimplemented filter %.*s\n",
                (int)name->value.size, name->value.data);
      }
    }
fallthrough:
    ;
    /* FALLTHROUGH */
  }
  case PrintRawData:
    iprint_binary_newline(options, &stream_data);
    // FIXME: maybe free uncompressed data?
    break;
  case PrintSummary:
    iprintf(options, "(%zu bytes)\n", stream->data.size);
    break;
  }

  iprintf(options, "endstream\n");
}

static void ast_print(struct OutputOptions* options, struct PDF* pdf,
                      const struct Object* object) {
  switch (object->kind) {
  case Boolean:
    iprintf(options, "%s\n", pdf->booleans[object->index].value ? "true" : "false");
    break;
  case Integer:
    iprintf(options, "%d\n", pdf->integers[object->index].value);
    break;
  case Real:
    iprintf(options, "%f\n", pdf->reals[object->index].value);
    break;
  case String:
    iprint_binary_newline(options, &pdf->strings[object->index].value);
    break;
  case Name:
    ast_print_name(options, &pdf->names[object->index], /*print_newline=*/true);
    break;
  case Array: {
    struct ArrayObject array = pdf->arrays[object->index];
    iprintf(options, "[\n");
    increase_indent(options);
    for (size_t i = 0; i < array.count; ++i)
      ast_print(options, pdf, &array.elements[i]);
    decrease_indent(options);
    iprintf(options, "]\n");
    break;
  }
  case Dictionary:
    ast_print_dict(options, pdf, &pdf->dicts[object->index]);
    break;
  case Stream:
    ast_print_stream(options, pdf, &pdf->streams[object->index]);
    break;
  case Null:
    iprintf(options, "null\n");
    break;
  case IndirectObject: {
    struct IndirectObjectObject indirect = pdf->indirect_objects[object->index];

    // FIXME: Only if it has the highest generation number.
    options->indirect_object_offsets[indirect.id] = options->bytes_written;

    iprintf(options, "%zu %zu obj\n", indirect.id, indirect.generation);
    if (indirect.value.kind != Stream)
      increase_indent(options);
    ast_print(options, pdf, &indirect.value);
    if (indirect.value.kind != Stream)
      decrease_indent(options);
    iprintf(options, "endobj\n");
    break;
  }
  case IndirectObjectRef:
    iprintf(options, "%zu %zu R\n",
      pdf->indirect_object_refs[object->index].id,
      pdf->indirect_object_refs[object->index].generation);
    break;
  case Comment:
    iprint_binary_newline(options, &pdf->comments[object->index].value);
    break;
  case XRef: {
    options->xref_offset = options->bytes_written;

    // FIXME: Fix up count if update_offsets (?)
    struct XRefObject xref = pdf->xrefs[object->index];
    iprintf(options, "xref\n%zu %zu\n", xref.start_id, xref.count);
    for (size_t i = 0; i < xref.count; ++i) {
      size_t offset = xref.entries[i].offset;

      // Optionally recompute offset so that it's correct for pretty-printed output.
      if (options->update_offsets && !xref.entries[i].is_free) {
        size_t id = xref.start_id + i;
        offset = options->indirect_object_offsets[id];
      }

      // FIXME: Reopen stdout as binary on windows :/
      nprintf(options, "%010zu %05zu %c \n", offset,
                                             xref.entries[i].generation,
                                             xref.entries[i].is_free ? 'f' : 'n');
    }
    break;
  }
  case Trailer:
    // FIXME: does anything in the trailer need optional recomputing?
    iprintf(options, "trailer\n");
    increase_indent(options);
    ast_print_dict(options, pdf, &pdf->trailers[object->index].dict);
    decrease_indent(options);
    break;
  case StartXRef: {
    size_t offset = pdf->start_xrefs[object->index].offset;

    // Optionally recompute offset so that it's correct for pretty-printed output.
    if (options->update_offsets)
      offset = options->xref_offset;

    iprintf(options, "startxref\n%zu\n", offset);
    break;
  }
  }
}

static struct Object* get_first_real_object(const struct PDF* pdf) {
  for (size_t i = 0; i < pdf->toplevel_objects_count; ++i)
    if (pdf->toplevel_objects[i].kind != Comment)
      return &pdf->toplevel_objects[i];
  return NULL;
}

static bool is_linearized(const struct PDF* pdf) {
  struct Object* first = get_first_real_object(pdf);
  if (!first)
    return false;

  if (first->kind != IndirectObject)
    return false;

  struct IndirectObjectObject* indirect = &pdf->indirect_objects[first->index];
  if (indirect->value.kind != Dictionary)
    return false;

  struct Object* linearized = dict_get(&pdf->dicts[indirect->value.index],
                                       "/Linearized");

  // FIXME: Could do more:
  // - check that `linearized` value is a number that's either 1 or 1.0
  // - check that the other required keys are present
  // - check that /Length is equal to length, else it is a file that
  //   used to be linearized that got an incremental update
  return linearized != NULL;
}

static void validate_startxref(struct Span data, struct PDF* pdf) {
  // FIXME: If is_linearized() is changed to return false if Length
  // doesn't match, this needs to become was_linearized_before_update()
  // since we still want to ignore the first startxref in that case).
  bool pdf_is_linearized = is_linearized(pdf);

  // `startxref` should point at `xref` table, or for a file using
  // xref streams, at an xref stream.
  // For a linearized PDF, the `startxref` in the first section is
  // optional and its offset is ignored (and is often `0`).
  for (size_t i = 0; i < pdf->start_xrefs_count; ++i) {
    size_t offset = pdf->start_xrefs[i].offset;

    // FIXME: This is for a linearized file. It'd be better to instead
    //        ignore the first `startxref` (in front of the first %%EOF).
    //        (example: veraPDFHiRes.pdf)
    if (pdf_is_linearized && i == 0 && offset == 0)
      continue;

    if (offset + strlen("xref") > data.size)
      fatal("`startxref` offset out of bounds\n");

    // FIXME: Instead of re-parsing, it'd be nicer if we'd keep a mapping
    //        from offset to object around and used that here.

    struct Token token;
    struct Span xref_data = { data.data + offset, data.size - offset };
    read_token_at_current_position(&xref_data, &token);
    if (token.kind == kw_xref)
      continue;

    // Didn't find `xref` at startxref offset. It could be an xref stream
    // instead. (XXX check if version is >= 1.5?)
    // (example: encryption_nocopy.pdf)
    if (token.kind == tok_number) {
      bool found = false;
      int32_t id = integer_value(&token);
      for (size_t i = 0; i < pdf->indirect_objects_count; ++i) {
        if ((size_t)id == pdf->indirect_objects[i].id) {
          if (pdf->indirect_objects[i].value.kind != Stream)
            fatal("`startxref` offset points at non-stream object\n");

          struct StreamObject* s =
            &pdf->streams[pdf->indirect_objects[i].value.index];
          struct Object* type = dict_get(&s->dict, "/Type");
          if (!type || type->kind != Name)
            fatal("`startxref` at stream object without /Type\n");

          if (strncmp((char*)pdf->names[type->index].value.data, "/XRef",
                      pdf->names[type->index].value.size))
            fatal("`startxref` at stream object without /Type /XRef\n");

          found = true;
          break;
        }
      }
      if (found)
        continue;
    }

    fatal("`startxref` offset points at neither `xref` nor xref stream\n");
  }
}

static void validate(struct Span data, struct PDF* pdf) {
  validate_startxref(data, pdf);
}

static void write_tiff_header(FILE* f, int K, uint32_t width, uint32_t height,
                              bool black_is_1, struct Span* data) {
  struct TiffHeader {
    char endianness[2]; // 'MM' for big-endian, 'II' for little-endian
    uint16_t forty_two; // magic number 42
    uint32_t ifd_offset;
  } header;

  header.endianness[0] = 'I';
  header.endianness[1] = 'I';
  header.forty_two = 42;
  header.ifd_offset = sizeof(struct TiffHeader);

  enum Type {
    Byte = 1,
    ASCII,
    Short,
    Long,
    Rational,

    SByte,
    Undefined,
    SShort,
    SLong,
    SRational,
    Float,
    Double,
  };

  struct IFDEntry {
    uint16_t tag_id;
    uint16_t type;
    uint32_t count;
    uint32_t data_offset;
  };

  uint16_t ifd_entry_count = 7;
  int strip_offset = sizeof(struct TiffHeader) + sizeof(uint16_t) +
      ifd_entry_count * sizeof(struct IFDEntry) + sizeof(uint32_t);

  // "Required Fields for Bilevel Images" in tiff spec:
  //    ImageWidth                256 100 SHORT or LONG
  //    ImageLength               257 101 SHORT or LONG
  //    Compression               259 103 SHORT         1, 2 or 32773
  //    PhotometricInterpretation 262 106 SHORT         0 or 1
  //    StripOffsets              273 111 SHORT or LONG
  //    RowsPerStrip              278 116 SHORT or LONG
  //    StripByteCounts           279 117 LONG or SHORT
  //    XResolution               282 11A RATIONAL
  //    YResolution               283 11B RATIONAL
  //    ResolutionUnit            296 128 SHORT         1, 2 or 3
  struct IFDEntry ifd[] = {
    { 256 /* width */, Long, 1, width },
    { 257 /* "length" (ie height) */, Long, 1, height },

    // 258 "bits per sample" defaults to 1.

    // Get /K off compression dict and write 3 (K > 0), 2 (K == 0), 4 (K < 0)
    // Since this has type Short but just writes the 1 into the uint32_t field,
    // this assumes little-endian layout. (Also below.)
    // 2 is CCITT Group 3 1-D (page 17 or 30 of the Tiff 6 spec)
    // 3 is CCITT T.4 bi-level (see p49 onward of the Tiff 6 spec)
    // 4 is CCITT T.6 bi-level (see p49 onward of the Tiff 6 spec)
    // FIXME: Might want to set 292 T4Options or 293 T6Options too?
    { 259 /* compression */, Short, 1, K < 0 ? 4 : (K == 0 ? 2 : 3 ) },

    // Write 0 or 1 based on if 0 is white (1) or black (0).
    { 262 /* photometric interpretation */, Short, 1, black_is_1 ? 1 : 0 },

    { 273 /* strip offsets */, Long, 1, strip_offset },

    // 277 "samples per pixel" defaults to 1.

    { 278 /* rows per strip */, Long, 1, height },
    { 279 /* strip byte counts */, Long, 1, data->size },

    // Let's cheat and omit resolution. (FIXME: Don't cheat.)
  };

  // Remember to update ifd_entry_count when adding entries.
  assert(ifd_entry_count == sizeof(ifd) / sizeof(ifd[0]));

  uint32_t next_ifd_offset = 0;

  fwrite(&header, sizeof(struct TiffHeader), 1, f);
  fwrite(&ifd_entry_count, sizeof(ifd_entry_count), 1, f);

  fwrite(ifd, sizeof(ifd), 1, f);

  fwrite(&next_ifd_offset, sizeof(next_ifd_offset), 1, f);
}

static void save_images(struct PDF* pdf) {

  // FIXME: While most images are indirect objects, a page's content
  // stream can also contain embedded images (BI, ID, EI in table 4.42,
  // "Inline image operators".

  for (size_t i = 0; i < pdf->indirect_objects_count; ++i) {
    struct Object* object = &pdf->indirect_objects[i].value;
    if (object->kind != Stream)
      continue;

    struct StreamObject* stream = &pdf->streams[object->index];
    struct Object* subtype = dict_get(&stream->dict, "/Subtype");
    if (!subtype || subtype->kind != Name)
      continue;
    struct NameObject* name = &pdf->names[subtype->index];
    if (strncmp((char*)name->value.data, "/Image", name->value.size))
      continue;

    printf("indirect object %zu is an image\n", pdf->indirect_objects[i].id);

    struct Object* filter = dict_get(&stream->dict, "/Filter");
    if (!filter)
      continue;
    if (filter->kind == Array) {
      struct ArrayObject* array = &pdf->arrays[filter->index];
      if (array->count != 1)
        fatal("can't handle filter arrays with size != 1");
      filter = &array->elements[0];
    }

    if (filter->kind != Name)
      fatal("unexpected /Filter type");
    name = &pdf->names[filter->index];

    struct Object* parms = dict_get(&stream->dict, "/DecodeParms");
    struct DictionaryObject* parms_dict = NULL;
    if (parms) {
      if (parms->kind == Array) {
        struct ArrayObject* array = &pdf->arrays[parms->index];
        if (array->count != 1)
          fatal("can't handle DecodeParms arrays with size != 1");
        parms = &array->elements[0];
      }
      if (parms->kind != Dictionary)
        fatal("unexpected /DecodeParms type");
      parms_dict = &pdf->dicts[parms->index];
    }

    bool need_tiff_header = false;
    char buf[80];
    if (!strncmp((char*)name->value.data, "/DCTDecode", name->value.size)) {
      sprintf(buf, "out_%d.jpg", (int)pdf->indirect_objects[i].id);
      printf("it's a jpeg! saving to %s\n", buf);
    } else if (!strncmp((char*)name->value.data, "/JPXDecode", name->value.size)) {
      sprintf(buf, "out_%d.jpx", (int)pdf->indirect_objects[i].id);
      printf("it's a jpeg2000! saving to %s\n", buf);
    } else if (!strncmp((char*)name->value.data, "/CCITTFaxDecode", name->value.size)) {
      sprintf(buf, "out_%d.tif", (int)pdf->indirect_objects[i].id);
      printf("it's a CCITT image! saving to %s\n", buf);
      need_tiff_header = true;
    } else {
      // FIXME: FlateDecode + Predictor 10-15: png IDAT
      // FIXME: FlateDecode + Predictor 2: TIFF
      //        (LZWDecode + Predictor are spec'd like FlateDecode + Predictor)
      // FIXME: JBIG2Decode
      // FIXME: LZWDecode
      // FIXME: FlateDecode without predictor needs encoding to some
      //        (lossless) format

      printf("don't know how to save filter type '%.*s' yet\n",
             (int)name->value.size, (char*)name->value.data);
      continue;
    }

    FILE* f = fopen(buf, "wb");
    if (!f)
      fatal("failed to open %s\n", buf);

    if (need_tiff_header) {
      // Table 3.9 Optional parameters for the CCITTFaxDecode filter
      // "K    A code identifying the encoding scheme used:
      // "< 0  Pure two-dimensional encoding (Group 4)
      //    0  Pure one-dimensional encoding (Group 3, 1-D)
      //  > 0  Mixed one- and two-dimensional encoding (Group 3, 2-D),
      //       in which a line encoded one-dimensionally can be followed
      //       by at most K - 1 lines encoded two-dimensionally."
      int K = 0; // "Default value: 0."
      struct Object* k_obj = parms_dict ? dict_get(parms_dict, "/K") : NULL;
      if (k_obj && k_obj->kind == Integer)
        K = pdf->integers[k_obj->index].value;

      int width = 1728; // "Default value: 1728" (!)
      struct Object* w_obj =
          parms_dict ? dict_get(parms_dict, "/Columns") : NULL;
      if (w_obj && w_obj->kind == Integer)
        width = pdf->integers[w_obj->index].value;

      int height = 0; // "Default value: 0"
      struct Object* h_obj =
          parms_dict ? dict_get(parms_dict, "/Rows") : NULL;
      if (h_obj && h_obj->kind == Integer)
        height = pdf->integers[h_obj->index].value;

      // "If the value is 0 or absent, the image's height is not
      //  predetermined, and the encoded data must be terminated by
      //  and end-of-block bit pattern or by the end of the filter's data.
      if (height == 0)
        fatal("cannot save CCITTFaxDecode with height 0");

      bool black_is_1 = false; // "Default value: false"
      struct Object* b_obj =
          parms_dict ? dict_get(parms_dict, "/BlackIs1") : NULL;
      if (b_obj && b_obj->kind == Boolean)
        black_is_1 = pdf->booleans[b_obj->index].value;

      write_tiff_header(f, K, width, height, black_is_1, &stream->data);
    }

    fwrite(stream->data.data, stream->data.size, 1, f);
    fclose(f);
  }
}

static void pretty_print(struct Span data, struct OutputOptions* options) {
  struct PDF pdf;
  init_pdf(&pdf);

  if (options->update_offsets)
    pdf.trust_stream_lengths = false;

  parse_pdf(data, &pdf);

  if (options->save_images) {
    save_images(&pdf);
    return;
  }

  if (options->quiet)
    return;

  bool do_validate = !options->update_offsets;
  if (do_validate)
    validate(data, &pdf);

  size_t max_indirect_object_id = 0;
  for (size_t i = 0; i < pdf.indirect_objects_count; ++i) {
    size_t id = pdf.indirect_objects[i].id;
    if (id > max_indirect_object_id)
      max_indirect_object_id = id;
  }

  options->indirect_object_offsets = calloc(max_indirect_object_id + 1,
                                            sizeof(size_t));

  nprintf(options, "%%PDF-%d.%d\n", pdf.version.major, pdf.version.minor);
  for (size_t i = 0; i < pdf.toplevel_objects_count; ++i)
    ast_print(options, &pdf, &pdf.toplevel_objects[i]);
}

int main(int argc, char* argv[]) {
  const char* program_name = argv[0];

  // Parse options.
  bool opt_dump_tokens = false;
  bool indent_output = true;
  bool save_images = false;
  bool uncompress = false;
  bool update_offsets = false;
  bool quiet = false;
#define kDumpTokens 512
#define kNoIndent 513
#define kSaveImages 514
#define kUncompress 515
#define kUpdateOffsets 516
#define kQuiet 517
  struct option getopt_options[] = {
      {"dump-tokens", no_argument, NULL, kDumpTokens},
      {"help", no_argument, NULL, 'h'},
      {"no-indent", no_argument, NULL, kNoIndent},
      {"save-images", no_argument, NULL, kSaveImages},
      {"uncompress", no_argument, NULL, kUncompress},
      {"update-offsets", no_argument, NULL, kUpdateOffsets},
      {"quiet", no_argument, NULL, kQuiet},
      {0, 0, 0, 0},
  };
  int opt;
  while ((opt = getopt_long(argc, argv, "h", getopt_options, NULL)) != -1) {
    switch (opt) {
      case 'h':
        print_usage(stdout, program_name);
        return 0;
      case '?':
        print_usage(stderr, program_name);
        return 1;
      case kDumpTokens:
        opt_dump_tokens = true;
        break;
      case kNoIndent:
        indent_output = false;
        break;
      case kSaveImages:
        save_images = true;
        break;
      case kUncompress:
        uncompress = true;
        break;
      case kUpdateOffsets:
        update_offsets = true;
        break;
      case kQuiet:
        quiet = true;
        break;
    }
  }
  argv += optind;
  argc -= optind;
  if (argc != 1) {
    fprintf(stderr, "expected 1 arg, got %d\n", argc);
    print_usage(stderr, program_name);

    return 1;
  }

  const char* in_name = argv[0];

  // Read input.
  int in_file = open(in_name, O_RDONLY);
  if (!in_file)
    fatal("Unable to read \'%s\'\n", in_name);

  struct stat in_stat;
  if (fstat(in_file, &in_stat))
    fatal("Failed to stat \'%s\'\n", in_name);

  void* contents = mmap(
      /*addr=*/0, in_stat.st_size,
      PROT_READ, MAP_SHARED, in_file, /*offset=*/0);
  if (contents == MAP_FAILED)
    fatal("Failed to mmap: %d (%s)\n", errno, strerror(errno));

  if (opt_dump_tokens)
    dump_tokens(&(struct Span){ contents, in_stat.st_size });
  else {
    struct OutputOptions options;
    init_output_options(&options);
    options.indent_output = indent_output;
    if (uncompress)
      options.stream_options = PrintRawDataUnfiltered;
    options.save_images = save_images;
    options.update_offsets = update_offsets;
    options.quiet = quiet;

    pretty_print((struct Span){ contents, in_stat.st_size }, &options);
  }

  munmap(contents, in_stat.st_size);
  close(in_file);
}
