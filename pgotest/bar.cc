#include <stdlib.h>
void f(int);
int main(int argc, char* argv []) {
  long l = strtol(argv[1], nullptr, 10);
  for (int i = 0; i < l; ++i)
    f(i);
}
