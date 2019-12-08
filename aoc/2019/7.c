#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

void swap(int* a, int* b) { int t = *a; *a = *b; *b = t; }

void reverse(int* first, int* last) {
  if (first != last) for (; first < --last; ++first) swap(first, last);
}

// No std::next_permutation() in C.
bool next_permutation(int* first, int* last) {
  // Assumes [first, last) contains at least 2 elements.
  int* i = last - 1;
  while (1) {
    int *i1 = i, *i2;
    if (*--i < *i1) {
      i2 = last;
      while (!(*i < *--i2))
        ;
      swap(i, i2);
      reverse(i1, last);
      return true;
    }
    if (i == first) {
      reverse(first, last);
      return false;
    }
  }
}

int main(void) {
  int orig_dat[1024];

  {
    char in[4096];
    if (fgets(in, sizeof(in), stdin) == NULL)
      return 1;

    char *str = in, *token; size_t i = 0;
    while ((token = strsep(&str, ",")) != NULL) {
      if (i >= sizeof(orig_dat) / sizeof(orig_dat[0]))
        return 2;
      orig_dat[i++] = atoi(token);
    }
  }

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

  int perm[] = { 0, 1, 2, 3, 4 };
  int max_out = 0;
  do {
    int prev_out = 0;
    for (int i = 0; i < 5; ++i) {
      int dat[sizeof(orig_dat)/sizeof(orig_dat[0])];
      memcpy(dat, orig_dat, sizeof(orig_dat));

      int in[] = { perm[i], prev_out };
      int* inp = in;

      int *ip = dat;
      goto *opcode[*ip];

in    : dat[ip[1]] = *inp++; ip += 2; goto *opcode[*ip];

out_m : prev_out = dat[ip[1]]; ip += 2; goto *opcode[*ip];
out_i : prev_out =     ip[1] ; ip += 2; goto *opcode[*ip];

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

done  : ;
    }
    if (prev_out > max_out)
      max_out = prev_out;
  } while (next_permutation(perm, perm + sizeof(perm) / sizeof(perm[0])));

  printf("%d\n", max_out);
}
