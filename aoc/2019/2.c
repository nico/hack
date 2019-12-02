#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  char in[4096];
  if (fgets(in, sizeof(in), stdin) == NULL)
    return 1;
  int dat[4096], i = 0;

  char* str = in, *token;
  while ((token = strsep(&str, ",")) != NULL) {
    if (i >= sizeof(dat)/sizeof(dat[0]))
      return 2;
    dat[i++] = atoi(token);
  }

  dat[1] = 12;
  dat[2] = 2;

  void *opcode[] = { [1] = &&add, [2] = &&mul, [99] = &&done };
  int *ip = dat;
  goto *opcode[*ip];

add:
  dat[ip[3]] = dat[ip[1]] + dat[ip[2]];
  ip += 4;
  goto *opcode[*ip];

mul:
  dat[ip[3]] = dat[ip[1]] * dat[ip[2]];
  ip += 4;
  goto *opcode[*ip];

done:
  printf("%d\n", dat[0]);
}
