#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <queue>

int main() {
  const int N = 10000;
  bool is_prime[N];
  is_prime[0] = is_prime[1] = 0;
  is_prime[2] = 1;
  for (int i = 3; i < N; ++i) is_prime[i] = i & 1;
  for (int i = 3; i <= sqrt(N); i += 2)
    if (is_prime[i])
      for (int j = 2 * i; j < N; j += i)
        is_prime[j] = false;
  std::fill_n(is_prime, 1000, false);  // "The first digit must be nonzero"

  int n; scanf("%d\n", &n);
  for (int i = 0; i < n; ++i) {
    unsigned from, to; scanf("%u %u\n", &from, &to);

    unsigned dist[N];
    std::fill_n(dist, N, UINT_MAX);

    std::queue<unsigned> q;
    q.push(from);
    dist[from] = 0;
    while (!q.empty() && q.front() != to) {
      unsigned now = q.front(); q.pop();
      for (int e = 10; e <= 10000; e *= 10) {
        for (int d = 0; d < 10; ++d) {
          unsigned neighbor = (now / e) * e + d * (e / 10) + now % (e / 10);
          if (is_prime[neighbor] && dist[neighbor] ==  UINT_MAX) {
            q.push(neighbor);
            dist[neighbor] = dist[now] + 1;
          }
        }
      }
    }

    if (q.empty())
      printf("Impossible\n");
     else
      printf("%u\n", dist[to]);
  }
}
