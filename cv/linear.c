#include "linear.h"

#include <assert.h>
#include <math.h>
#include <string.h>

// Solves m * x = rhs for x, stores x in rhs.
static bool solve(float m[8][8], float rhs[8]) {
  const int rows = 8;
  bool col_used[8] = {};

  for (int row = 0; row < rows; ++row) {
    float max_val = 0;
    int curr_pivot_row = -1, curr_pivot_col = -1;
    // Search for biggest number in matrix, use it as pivot.
    for (int i = 0; i < rows; ++i) {
      if (col_used[i]) continue;

      for (int j = 0; j < rows; ++j) {
        if (col_used[j]) continue;

        float curr = fabs(m[j][i]);
        if (curr > max_val) {
          max_val = curr;
          curr_pivot_row = j;
          curr_pivot_col = i;
        }
      }
    }

    assert(curr_pivot_row != -1 && curr_pivot_col != -1);
    if (col_used[curr_pivot_col]) {
      assert(false);
      return false;
    }
    col_used[curr_pivot_col] = true;

    // Swap rows to bring pivot on diagonal.
    if (curr_pivot_row != curr_pivot_col) {
      for (int i = 0; i < rows; ++i) {
        float t = m[curr_pivot_row][i];
        m[curr_pivot_row][i] = m[curr_pivot_col][i];
        m[curr_pivot_col][i] = t;
      }
      float t = rhs[curr_pivot_row];
      rhs[curr_pivot_row] = rhs[curr_pivot_col];
      rhs[curr_pivot_col] = t;
      curr_pivot_row = curr_pivot_col;
    }

    if (m[curr_pivot_row][curr_pivot_col] == 0) {
      assert(false);
      return false;
    }

    // Start eliminating the pivot's column in each row.
    float inverse_pivot = 1.f / m[curr_pivot_row][curr_pivot_col];

    m[curr_pivot_row][curr_pivot_col] = 1;
    for (int i = 0; i < rows; ++i)
      m[curr_pivot_row][i] *= inverse_pivot;
    rhs[curr_pivot_row] *= inverse_pivot;

    // Reduce non-pivot rows.
    for (int i = 0; i < rows; ++i) {
      if (i == curr_pivot_row)  // Pivot row is already reduced.
        continue;

      float temp = m[i][curr_pivot_row];
      m[i][curr_pivot_row] = 0;
      for (int j = 0; j < rows; ++j)
        m[i][j] -= temp*m[curr_pivot_row][j];
      rhs[i] -= rhs[curr_pivot_row] * temp;
    }
  }

  return true;
}

bool compute_projection_matrix(float m[3][3], float x[4][2], float b[4][2]) {

  // http://alumni.media.mit.edu/~cwren/interpolator/
  float mat[8][8];
  float rhs[8];
  for (int i = 0; i < 4; ++i) {
    float xrow[] = {
      x[i][0], x[i][1], 1, 0, 0, 0, -b[i][0] * x[i][0], -b[i][0] * x[i][1]
    };
    memcpy(mat[2*i], xrow, sizeof(xrow));
    rhs[2*i] = b[i][0];

    float yrow[] = {
      0, 0, 0, x[i][0], x[i][1], 1, -b[i][1] * x[i][0], -b[i][1] * x[i][1]
    };
    memcpy(mat[2*i + 1], yrow, sizeof(yrow));
    rhs[2*i + 1] = b[i][1];
  }
  if (!solve(mat, rhs))
    return false;

  m[0][0] = rhs[0];
  m[0][1] = rhs[1];
  m[0][2] = rhs[2];

  m[1][0] = rhs[3];
  m[1][1] = rhs[4];
  m[1][2] = rhs[5];

  m[2][0] = rhs[6];
  m[2][1] = rhs[7];
  m[2][2] = 1;
  return true;
}
