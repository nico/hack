#ifndef LINEAR_H_
#define LINEAR_H_

#include <stdbool.h>

// Computes m so that m * x[i] = b[i] for every 2d point in x[i], b[i].
// m is row-major: m[row][col].
bool compute_projection_matrix(float m[3][3], float x[4][2], float b[4][2]);

#endif  // LINEAR_H_
