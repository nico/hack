#include <inttypes.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

const int NX = 50;
const int NY = 50;

int run(int64_t *dat, int x, int y) {
#define ENTRY0_1(n, l) \
  [      n] = &&l##_m,    [  10##n] = &&l##_i,    [  20##n] = &&l##_r
#define ENTRY1_0(n, l) \
  [      n] = &&l,        [  20##n] = &&l##_r
#define ENTRY0_2(n, l)                                                    \
  [      n] = &&l##_mm,   [  10##n] = &&l##_im,   [  20##n] = &&l##_rm, \
  [ 100##n] = &&l##_mi,   [ 110##n] = &&l##_ii,   [ 120##n] = &&l##_ri, \
  [ 200##n] = &&l##_mr,   [ 210##n] = &&l##_ir,   [ 220##n] = &&l##_rr
#define ENTRY1_2(n, l)                                                    \
  [      n] = &&l##  _mm, [  10##n] = &&l##_im,   [  20##n] = &&l##_rm,   \
  [ 100##n] = &&l##  _mi, [ 110##n] = &&l##_ii,   [ 120##n] = &&l##_ri,   \
  [ 200##n] = &&l##  _mr, [ 210##n] = &&l##_ir,   [ 220##n] = &&l##_rr,   \
  [2000##n] = &&l##_r_mm, [2010##n] = &&l##_r_im, [2020##n] = &&l##_r_rm, \
  [2100##n] = &&l##_r_mi, [2110##n] = &&l##_r_ii, [2120##n] = &&l##_r_ri, \
  [2200##n] = &&l##_r_mr, [2210##n] = &&l##_r_ir, [2220##n] = &&l##_r_rr

  const void* opcode[] = {
      ENTRY1_2(1, add),
      ENTRY1_2(2, mul),
      ENTRY1_0(3, in),
      ENTRY0_1(4, out),
      ENTRY0_2(5, je),
      ENTRY0_2(6, jne),
      ENTRY1_2(7, lt),
      ENTRY1_2(8, eq),
      ENTRY0_1(9, bas),
      [99] = &&done,
  };

  int res = -1, in_op, step = 0;

  int64_t base = 0;
  int64_t *ip = dat;
  goto *opcode[*ip];

in    : dat[ip[1]       ] = step++ ? x : y; ip += 2; goto *opcode[*ip];
in_r  : dat[ip[1] + base] = step++ ? x : y; ip += 2; goto *opcode[*ip];

#define CMD0_1(name) \
name##_m: CODE(dat[ip[1]       ]); \
name##_i: CODE(    ip[1]        ); \
name##_r: CODE(dat[ip[1] + base])

#define CODE(op1) in_op = op1; ip += 2; goto do_out
CMD0_1(out);
#undef CODE

do_out:
  if (step != 2 || res != -1) {
    fprintf(stderr, "what %d %d\n", step, in_op);
    exit(1);
  }
  res = in_op;
  goto *opcode[*ip];

#define CODE(op1) base += op1; ip += 2; goto *opcode[*ip]
CMD0_1(bas);
#undef CODE

#undef CMD0_1

#define CMD1_2(name) \
name##_mm  : CODE(dat[ip[3]       ], dat[ip[1]        ], dat[ip[2]       ]); \
name##_mi  : CODE(dat[ip[3]       ], dat[ip[1]        ],     ip[2]        ); \
name##_mr  : CODE(dat[ip[3]       ], dat[ip[1]        ], dat[ip[2] + base]); \
name##_im  : CODE(dat[ip[3]       ],     ip[1]         , dat[ip[2]       ]); \
name##_ii  : CODE(dat[ip[3]       ],     ip[1]         ,     ip[2]        ); \
name##_ir  : CODE(dat[ip[3]       ],     ip[1]         , dat[ip[2] + base]); \
name##_rm  : CODE(dat[ip[3]       ], dat[ip[1] + base] , dat[ip[2]       ]); \
name##_ri  : CODE(dat[ip[3]       ], dat[ip[1] + base] ,     ip[2]        ); \
name##_rr  : CODE(dat[ip[3]       ], dat[ip[1] + base] , dat[ip[2] + base]); \
name##_r_mm: CODE(dat[ip[3] + base], dat[ip[1]        ], dat[ip[2]       ]); \
name##_r_mi: CODE(dat[ip[3] + base], dat[ip[1]        ],     ip[2]        ); \
name##_r_mr: CODE(dat[ip[3] + base], dat[ip[1]        ], dat[ip[2] + base]); \
name##_r_im: CODE(dat[ip[3] + base],     ip[1]         , dat[ip[2]       ]); \
name##_r_ii: CODE(dat[ip[3] + base],     ip[1]         ,     ip[2]        ); \
name##_r_ir: CODE(dat[ip[3] + base],     ip[1]         , dat[ip[2] + base]); \
name##_r_rm: CODE(dat[ip[3] + base], dat[ip[1] + base] , dat[ip[2]       ]); \
name##_r_ri: CODE(dat[ip[3] + base], dat[ip[1] + base] ,     ip[2]        ); \
name##_r_rr: CODE(dat[ip[3] + base], dat[ip[1] + base] , dat[ip[2] + base])

#define CODE(dst, op1, op2) dst = op1  + op2; ip += 4; goto *opcode[*ip]
CMD1_2(add);
#undef CODE

#define CODE(dst, op1, op2) dst = op1  * op2; ip += 4; goto *opcode[*ip]
CMD1_2(mul);
#undef CODE

#define CODE(dst, op1, op2) dst = op1  < op2; ip += 4; goto *opcode[*ip]
CMD1_2(lt);
#undef CODE

#define CODE(dst, op1, op2) dst = op1 == op2; ip += 4; goto *opcode[*ip]
CMD1_2(eq);
#undef CODE

#undef CMD1_2

#define CMD0_2(name) \
name##_mm: CODE(dat[ip[1]        ], dat[ip[2]       ]); \
name##_mi: CODE(dat[ip[1]        ],     ip[2]        ); \
name##_mr: CODE(dat[ip[1]        ], dat[ip[2] + base]); \
name##_im: CODE(    ip[1]         , dat[ip[2]       ]); \
name##_ii: CODE(    ip[1]         ,     ip[2]        ); \
name##_ir: CODE(    ip[1]         , dat[ip[2] + base]); \
name##_rm: CODE(dat[ip[1] + base] , dat[ip[2]       ]); \
name##_ri: CODE(dat[ip[1] + base] ,     ip[2]        ); \
name##_rr: CODE(dat[ip[1] + base] , dat[ip[2] + base])

#define CODE(op1, op2) if ( op1) ip = &dat[op2]; else ip += 3; goto *opcode[*ip]
CMD0_2(je);
#undef CODE

#define CODE(op1, op2) if (!op1) ip = &dat[op2]; else ip += 3; goto *opcode[*ip]
CMD0_2(jne);
#undef CODE

#undef CMD0_2

done  : return res;
}

int main(void) {
  int64_t dat[4096];

  {
    char in[8000];
    if (fgets(in, sizeof(in), stdin) == NULL)
      return 1;
    if (in[strlen(in) - 1] != '\n') {
      fprintf(stderr, "program too long to read\n");
      return 1;
    }

    char *str = in, *token; size_t i = 0;
    while ((token = strsep(&str, ",")) != NULL) {
      if (i >= sizeof(dat) / sizeof(dat[0]))
        return 2;
      dat[i++] = strtoll(token, (char**)NULL, 10);
    }
  }

  const int NX = 50, NY = 50;
  int cnt = 0;
  for (int y = 0; y < NY; ++y) {
    for (int x = 0; x < NX; ++x) {
      int64_t scratch[sizeof(dat)/sizeof(dat[0])];
      memcpy(scratch, dat, sizeof(dat));
      int res = run(scratch, x, y);
      printf(res > 0 ? "#" : ".");
      if (res > 0)
        ++cnt;
    }
    printf("\n");
  }
  printf("%d\n", cnt);
}
