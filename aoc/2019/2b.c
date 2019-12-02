#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  char in[4096];
  if (fgets(in, sizeof(in), stdin) == NULL)
    return 1;
  int dat[1024], i = 0;

  char* str = in, *token;
  while ((token = strsep(&str, ",")) != NULL) {
    if (i >= sizeof(dat)/sizeof(dat[0]))
      return 2;
    dat[i++] = atoi(token);
  }

  void *opcode[] = { [1] = &&add, [2] = &&mul, [99] = &&done };

  for (int noun = 0; noun < 100; ++noun) {
    for (int verb = 0; verb < 100; ++verb) {
      int scratch[sizeof(dat)/sizeof(dat[0])];
      memcpy(scratch, dat, sizeof(dat));
      scratch[1] = noun;
      scratch[2] = verb;

      int *ip = scratch;
      goto *opcode[*ip];

add:
      scratch[ip[3]] = scratch[ip[1]] + scratch[ip[2]];
      ip += 4;
      goto *opcode[*ip];

mul:
      scratch[ip[3]] = scratch[ip[1]] * scratch[ip[2]];
      ip += 4;
      goto *opcode[*ip];

done:
      if (scratch[0] == 19690720)
        printf("%d\n", 100 * noun + verb);
    }
  }

}
