#include <inttypes.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int bfs(uint8_t* grid, const int NX, const int NY,
        int sx, int sy, int dx, int dy) {
  int q[NX*NY][2];

  int qs = 0, qe = 1;
  q[qs][0] = sx;
  q[qs][1] = sy;

  uint8_t visited[NY][NX];
  int best[NY][NX];  // XXX could be just a pair of (length, n_frontier)
  memset(visited, 0, NY*NX);
  memset(best, 0, NY*NX * sizeof(int));

  int d[4][2] = { {0, -1}, {0, 1}, {-1, 0}, {1, 0} };
  while (qs != qe) {
    int cx = q[qs][0];
    int cy = q[qs][1];
    qs++;

    for (int i = 0; i < 4; ++i) {
      int nx = cx + d[i][0];
      int ny = cy + d[i][1];
      if (!visited[ny][nx]) {
        visited[ny][nx] = true;
        best[ny][nx] = best[cy][cx] + 1;

        if (grid[ny * NX + nx] == 0)
          return -1;  // Found unknown field.
        if (grid[ny * NX + nx] == 3)
          return best[ny][nx];
        else if (grid[ny * NX + nx] == 2) {
          q[qe][0] = nx;
          q[qe][1] = ny;
          qe++;
        }
      }
    }
  }

  return -2;  // Didn't find path.
}

int ai() {
  return rand() % 4 + 1;
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

  const int NX = 43;
  const int NY = 42;
  uint8_t grid[NY][NX] = {0};
  enum { kX, kY, kTile } state = kX;
  int dir, x = NX/2, y = NY/2, in_op, nx, ny;
  int sx = x, sy = y, dx = -1, dy = -1;
  grid[y][x] = 4;
  int d[5][2] = { {0, 0}, {0, -1}, {0, 1}, {-1, 0}, {1, 0} };
  int64_t i = 0;


  int64_t base = 0;
  int64_t *ip = dat;
  goto *opcode[*ip];

in    : dat[ip[1]       ] = dir = ai(); ip += 2; goto *opcode[*ip];
in_r  : dat[ip[1] + base] = dir = ai(); ip += 2; goto *opcode[*ip];

#define CMD0_1(name) \
name##_m: CODE(dat[ip[1]       ]); \
name##_i: CODE(    ip[1]        ); \
name##_r: CODE(dat[ip[1] + base])

#define CODE(op1) in_op = op1; ip += 2; goto move
CMD0_1(out);
#undef CODE

move:
  nx = x + d[dir][0];
  ny = y + d[dir][1];
  if (nx < 0 || nx >= NX || ny < 0 || ny >= NY) {
    fprintf(stderr, "out of bounds %d %d\n", x, y);
    exit(1);
  }
  if (in_op == 0)
    grid[ny][nx] = 1;
  else {
    x = nx;
    y = ny;
    if (in_op == 2) {
      dx = x;
      dy = y;
    }
    if (!grid[ny][nx])
      grid[ny][nx] = 2 + (in_op == 2);
  }

  if (i++ % 200000 == 0) {
    char chars[] = { '?', '#', ' ', 'x', 'o' };
    for (int iy = 0; iy < NY; ++iy) {
      for (int ix = 0; ix < NX; ++ix)
        printf("%c", chars[grid[iy][ix]]);
      printf("\n");
    }
    printf("\n");

    // bfs from (sx, sy) to (dx, dy), abort if it hits a ? along the way.
    if (dx != -1) {
      int bfsr = bfs(&grid[0][0], NX, NY, sx, sy, dx, dy);
      if (bfsr != -1) {
        printf("%d\n", bfsr);
        exit(0);
      }
    }
  }

  state = kX;
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

done  : return 0;
}
