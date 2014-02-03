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
void read_token(Input* input, Token* t);

int main(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    Input* input = open_file(argv[i]);
    if (!input) {
      fprintf(stderr, "failed to open %s\n", argv[i]);
      continue;
    }
    close_file(input);
  }
}
