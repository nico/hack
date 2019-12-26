#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

const int DAT_N = 4096;
const int IN_N = 20;

struct State {
  int64_t dat[DAT_N];
  int64_t *ip;
  int64_t base;

  int64_t in_queue[IN_N];
  int in_head, in_tail;
};

int64_t read(struct State* s) {
  if (s->in_head == s->in_tail)
    return -1;
  int64_t t = s->in_queue[s->in_head++];
  if (s->in_head >= IN_N) s->in_head = 0;
  return t;
}

void write(struct State* s, int64_t i) {
  s->in_queue[s->in_tail++] = i;
  if (s->in_tail >= IN_N) s->in_tail = 0;
  if (s->in_tail == s->in_head) {
    fprintf(stderr, "queue full");
    exit(42);
  }
}

int main(void) {
  int64_t orig_dat[DAT_N];

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
      if (i >= DAT_N) {
        fprintf(stderr, "program at least %d codes long\n", (int)i);
        return 2;
      }
      orig_dat[i++] = strtoll(token, (char**)NULL, 10);
    }
  }

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

  const int N = 50;

  struct State state[N];
  for (int i = 0; i < N; ++i) {
    memcpy(state[i].dat, orig_dat, sizeof(orig_dat));
    state[i].ip = state[i].dat;
    state[i].base = 0;
    state[i].in_head = 0;
    state[i].in_queue[0] = i;
    state[i].in_tail = 1;
  }

  int64_t nat_x, nat_y;

  while (true) {
    bool net_idle = true;
    for (int i = 0; i < N; ++i) {
      int64_t *dat = state[i].dat;
      int64_t *ip = state[i].ip;
      int64_t base = state[i].base;
      int64_t in_op, addr, op1, op2;

      if (state[i].in_head != state[i].in_tail)
        net_idle = false;

      enum { kWriteAddr, kWriteOp1, kWriteOp2 } write_state = kWriteAddr;
      goto *opcode[*ip];

in    : dat[ip[1]       ] = read(state + i); ip += 2; goto next;
in_r  : dat[ip[1] + base] = read(state + i); ip += 2; goto next;

#define CMD0_1(name) \
name##_m: CODE(dat[ip[1]       ]); \
name##_i: CODE(    ip[1]        ); \
name##_r: CODE(dat[ip[1] + base])

#define CODE(op1) in_op = op1; ip += 2; goto out;
CMD0_1(out);
#undef CODE

out:
  net_idle = false;
  if (write_state == kWriteAddr) {
    addr = in_op;
    write_state = kWriteOp1;
  } else if (write_state == kWriteOp1) {
    op1 = in_op;
    write_state = kWriteOp2;
  } else {
    op2 = in_op;
    write_state = kWriteAddr;

    printf("send to %d: %d %d\n", (int)addr, (int)op1, (int)op2);
    if (0 <= addr && addr < N) {
      write(state + addr, op1);
      write(state + addr, op2);
    } else if (addr == 255) {
      nat_x = op1;
      nat_y = op2;
    }
    // context switch instead. assumes no read happens inside a 3-read.
    goto next;
  }
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

done  :
next  :
      state[i].ip = ip;
      state[i].base = base;
    }
    if (net_idle) {
      printf("NAT send to 0: %d %d\n", (int)nat_x, (int)nat_y);
      write(state, nat_x);
      write(state, nat_y);
    }
  }
}
