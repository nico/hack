#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
using namespace std;

int ReadFile(const string& path, string* contents, string* err) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) {
    err->assign(strerror(errno));
    return -errno;
  }

  char buf[64 << 10];
  size_t len;
  while ((len = fread(buf, 1, sizeof(buf), f)) > 0) {
    contents->append(buf, len);
  }
  if (ferror(f)) {
    err->assign(strerror(errno));  // XXX errno?
    contents->clear();
    fclose(f);
    return -errno;
  }
  fclose(f);
  return 0;
}

enum TokenKind {
  Whitespace,
  EndOfLine,
  Delimiter,
  Literal,
};

struct Token {
  TokenKind kind;
  char spelling;
};

TokenKind ReadToken(const char** cursor) {
  const char* p = *cursor;
  const char* q;
  TokenKind token;
  for (;;) {
    /*!re2c
    re2c:define:YYCTYPE = "unsigned char";
    re2c:define:YYCURSOR = p;
    re2c:define:YYMARKER = q;
    re2c:yyfill:enable = 0;

    eol = "\r\n"|"\r"|"\n";
    whitespace = [\t\n\f\r ]|"\000";
    delim = [{}<>[\]{}/%];

    whitespace*"%"[^whitespace]*eol { continue; }
    eol { token = EndOfLine; }
    whitespace { token = Whitespace; }
    delim { token = Delimiter; }
    */
  }

  *cursor = p;
  return token;
}

void Load(const char* f) {
  while (*f) {
    ReadToken(&f);
  }
}

int main(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    string contents, err;
    if (ReadFile(argv[i], &contents, &err) != 0)
      fprintf(stderr, "%s: %s\n", argv[i], err.c_str());
    Load(contents.c_str());
  }
}
