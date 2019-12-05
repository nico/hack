#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  int dat[1024];

  {
    char in[4096];
    if (fgets(in, sizeof(in), stdin) == NULL)
      return 1;

    char *str = in, *token; size_t i = 0;
    while ((token = strsep(&str, ",")) != NULL) {
      if (i >= sizeof(dat) / sizeof(dat[0]))
        return 2;
      dat[i++] = atoi(token);
    }
  }

  const int kInput = 5;
  const void* opcode[] = {
      [1] = &&add_mm, [101] = &&add_im, [1001] = &&add_mi, [1101] = &&add_ii,
      [2] = &&mul_mm, [102] = &&mul_im, [1002] = &&mul_mi, [1102] = &&mul_ii,
      [3] = &&in,
      [4] = &&out_m,  [104] = &&out_i,
      [5] = &&je_mm,  [105] = &&je_im,  [1005] = &&je_mi,  [1105] = &&je_ii,
      [6] = &&jne_mm, [106] = &&jne_im, [1006] = &&jne_mi, [1106] = &&jne_ii,
      [7] = &&lt_mm,  [107] = &&lt_im,  [1007] = &&lt_mi,  [1107] = &&lt_ii,
      [8] = &&eq_mm,  [108] = &&eq_im,  [1008] = &&eq_mi,  [1108] = &&eq_ii,
      [99] = &&done,
  };

  int *ip = dat;
  goto *opcode[*ip];

in    : dat[ip[1]] = kInput; ip += 2; goto *opcode[*ip];

out_m : printf("%d\n", dat[ip[1]]); ip += 2; goto *opcode[*ip];
out_i : printf("%d\n",     ip[1] ); ip += 2; goto *opcode[*ip];

add_mm: dat[ip[3]] = dat[ip[1]]  + dat[ip[2]]; ip += 4; goto *opcode[*ip];
add_mi: dat[ip[3]] = dat[ip[1]]  +     ip[2] ; ip += 4; goto *opcode[*ip];
add_im: dat[ip[3]] =     ip[1]   + dat[ip[2]]; ip += 4; goto *opcode[*ip];
add_ii: dat[ip[3]] =     ip[1]   +     ip[2] ; ip += 4; goto *opcode[*ip];

mul_mm: dat[ip[3]] = dat[ip[1]]  * dat[ip[2]]; ip += 4; goto *opcode[*ip];
mul_mi: dat[ip[3]] = dat[ip[1]]  *     ip[2] ; ip += 4; goto *opcode[*ip];
mul_im: dat[ip[3]] =     ip[1]   * dat[ip[2]]; ip += 4; goto *opcode[*ip];
mul_ii: dat[ip[3]] =     ip[1]   *     ip[2] ; ip += 4; goto *opcode[*ip];

lt_mm : dat[ip[3]] = dat[ip[1]]  < dat[ip[2]]; ip += 4; goto *opcode[*ip];
lt_mi : dat[ip[3]] = dat[ip[1]]  <     ip[2] ; ip += 4; goto *opcode[*ip];
lt_im : dat[ip[3]] =     ip[1]   < dat[ip[2]]; ip += 4; goto *opcode[*ip];
lt_ii : dat[ip[3]] =     ip[1]   <     ip[2] ; ip += 4; goto *opcode[*ip];

eq_mm : dat[ip[3]] = dat[ip[1]] == dat[ip[2]]; ip += 4; goto *opcode[*ip];
eq_mi : dat[ip[3]] = dat[ip[1]] ==     ip[2] ; ip += 4; goto *opcode[*ip];
eq_im : dat[ip[3]] =     ip[1]  == dat[ip[2]]; ip += 4; goto *opcode[*ip];
eq_ii : dat[ip[3]] =     ip[1]  ==     ip[2] ; ip += 4; goto *opcode[*ip];

je_mm : if ( dat[ip[1]]) ip = &dat[dat[ip[2]]]; else ip += 3; goto *opcode[*ip];
je_mi : if ( dat[ip[1]]) ip = &dat[    ip[2] ]; else ip += 3; goto *opcode[*ip];
je_im : if (     ip[1] ) ip = &dat[dat[ip[2]]]; else ip += 3; goto *opcode[*ip];
je_ii : if (     ip[1] ) ip = &dat[    ip[2] ]; else ip += 3; goto *opcode[*ip];

jne_mm: if (!dat[ip[1]]) ip = &dat[dat[ip[2]]]; else ip += 3; goto *opcode[*ip];
jne_mi: if (!dat[ip[1]]) ip = &dat[    ip[2] ]; else ip += 3; goto *opcode[*ip];
jne_im: if (!    ip[1] ) ip = &dat[dat[ip[2]]]; else ip += 3; goto *opcode[*ip];
jne_ii: if (!    ip[1] ) ip = &dat[    ip[2] ]; else ip += 3; goto *opcode[*ip];

done  : return 0;
}
