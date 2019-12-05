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

  void* opcode[] = {
      [   1] = &&add_mm,
      [ 101] = &&add_im,
      [1001] = &&add_mi,
      [1101] = &&add_ii,

      [   2] = &&mul_mm,
      [ 102] = &&mul_im,
      [1002] = &&mul_mi,
      [1102] = &&mul_ii,

      [   3] = &&read,

      [   4] = &&write_m,
      [ 104] = &&write_i,

      [  99] = &&done,
  };
  int *ip = dat;
  goto *opcode[*ip];

add_mm:
  dat[ip[3]] = dat[ip[1]] + dat[ip[2]];
  ip += 4;
  goto *opcode[*ip];
add_mi:
  dat[ip[3]] = dat[ip[1]] + ip[2];
  ip += 4;
  goto *opcode[*ip];
add_im:
  dat[ip[3]] = ip[1] + dat[ip[2]];
  ip += 4;
  goto *opcode[*ip];
add_ii:
  dat[ip[3]] = ip[1] + ip[2];
  ip += 4;
  goto *opcode[*ip];

mul_mm:
  dat[ip[3]] = dat[ip[1]] * dat[ip[2]];
  ip += 4;
  goto *opcode[*ip];
mul_mi:
  dat[ip[3]] = dat[ip[1]] * ip[2];
  ip += 4;
  goto *opcode[*ip];
mul_im:
  dat[ip[3]] = ip[1] * dat[ip[2]];
  ip += 4;
  goto *opcode[*ip];
mul_ii:
  dat[ip[3]] = ip[1] * ip[2];
  ip += 4;
  goto *opcode[*ip];

read:
  dat[ip[1]] = 1;
  ip += 2;
  goto *opcode[*ip];

write_m:
  printf("%d (%d)\n", dat[ip[1]], ip[1]);
  ip += 2;
  goto *opcode[*ip];
write_i:
  printf("%d\n", ip[1]);
  ip += 2;
  goto *opcode[*ip];

done:
  return 0;
}
