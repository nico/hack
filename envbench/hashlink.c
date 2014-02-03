#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  FILE* f;
} Input;

Input* open_file(const char* name) {
  FILE* f = fopen(name, "r");
  if (!f)
    return NULL;

  Input* input = malloc(sizeof(Input));
  input->f = f;
  return input;
}

void close_file(Input* input) {
  fclose(input->f);
  free(input);
}

typedef enum {
  kTokEof,
  kTokSet,
  kTokGet,
  kTokOpenScope,
  kTokCloseScope,
  kTokIdent,
} TokenType;
typedef struct {
  TokenType type;
  char* ident_text;
} Token;
void read_token(Input* input, Token* t) {
  if (feof(input->f)) {
    t->type = kTokEof;
    return;
  }

  int last_char = ' ';
  while (isspace(last_char))
    last_char = fgetc(input->f);

  if (last_char == '{') {
    t->type = kTokOpenScope;
    return;
  }
  if (last_char == '}') {
    t->type = kTokCloseScope;
    return;
  }
  t->type = kTokIdent;
}

void parse(Input* input) {
  int nesting_depth = 0;
  Token token;
  for (;;) {
    read_token(input, &token);
    switch (token.type) {
      case kTokEof:
        if (nesting_depth != 0)
          fprintf(stderr, "unbalanced scopes (%d)\n", nesting_depth);
        return;
      case kTokOpenScope:
        ++nesting_depth;
        break;
      case kTokCloseScope:
        --nesting_depth;
        break;
    }
  }
}

int main(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    Input* input = open_file(argv[i]);
    if (!input) {
      fprintf(stderr, "failed to open %s\n", argv[i]);
      continue;
    }
    parse(input);
    close_file(input);
  }
}
