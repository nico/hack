#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

  const int BUFSIZE = 1024;
  char buf[BUFSIZE];
  int buf_end = 0;
  do {
    if (buf_end >= BUFSIZE - 1) {
      fprintf(stderr, "buffer overflow");
      exit(1);
    }
    buf[buf_end++] = last_char;
    last_char = fgetc(input->f);
  } while (isalnum(last_char));
  buf[buf_end] = '\0';
  ungetc(last_char, input->f);

  if (strcmp(buf, "set") == 0) {
    t->type = kTokSet;
    return;
  }
  if (strcmp(buf, "get") == 0) {
    t->type = kTokGet;
    return;
  }

  t->type = kTokIdent;
  t->ident_text = strdup(buf);  // Leaks.
}

typedef struct HashNode {
  char* key;
  char* value;
  struct HashNode* next;
} HashNode;
typedef struct Hash {
  HashNode* nodes;
  struct Hash* parent;
} Hash;
Hash* hash_new(Hash* parent) {
  Hash* hash = malloc(sizeof(Hash));
  hash->parent = parent;
  return hash;
}

void hash_free(Hash* hash) {
  free(hash);
}
void hash_set(char* key, char* value);
char* hash_get(char* key);

void parse(Input* input) {
  Hash* hash = hash_new(NULL);

  int nesting_depth = 0;
  Token token;
  for (;;) {
    read_token(input, &token);
    switch (token.type) {
      case kTokEof:
        if (nesting_depth != 0)
          fprintf(stderr, "unbalanced scopes (%d)\n", nesting_depth);
        hash_free(hash);
        return;
      case kTokOpenScope:
        hash = hash_new(hash);
        ++nesting_depth;
        break;
      case kTokCloseScope:
        if (nesting_depth <= 0) {
          fprintf(stderr, "no scope to close\n");
          exit(1);
        }
        Hash* old = hash;
        hash = hash->parent;
        hash_free(old);

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
