#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t eval(char** c) {
  int64_t res;

  if (**c != '(') return -1;
  *c += 1;

  if (**c != '(') {
    res = strtol(*c, c, 10);
  } else {
    int64_t lhs = eval(c);
    char op = (*c)[1];
    *c += 3;
    int64_t rhs = eval(c);
    switch (op) {
      case '+': res = lhs + rhs; break;
      case '-': res = lhs - rhs; break;
      case '*': res = lhs * rhs; break;
    }
  }

  if (**c != ')') return -1;
  *c += 1;
  return res;
}

int main() {
  char buf[512];
  FILE* in = popen("./times-up-again", "r");

  if (!in || !fgets(buf, sizeof(buf), in) || buf[strlen(buf) - 1] != '\n')
    return 1;

  puts(buf);
  char* s = buf + strlen("Challenge: ");
  int64_t r = eval(&s);
  printf("%" PRId64 "\n", r);

  if (pclose(in) != 0)
    return 2;
}
