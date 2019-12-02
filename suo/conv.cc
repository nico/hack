// Like conv.py, but in C, and computes DIFAT blocks too.
// Also includes an extra-naive algorithm.
// clang -O2 conv.cc -Wall -Wextra -Wconversion && ./a.out

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include <functional>
#include <thread>
#include <vector>

typedef uint64_t N;

void naive(N n_blocks, N* out_n_difat, N* out_n_fat) {
  N n_difat = 0, n_fat = 0;

  while (true) {
    N n_possible_fat = 109 + 127 * n_difat;
    if (n_possible_fat < n_fat) {
      ++n_difat;
      continue;
    }

    N n_possible_blocks = 128 * n_fat;
    if (n_possible_blocks < n_blocks + n_fat + n_difat) {
      ++n_fat;
      continue;
    }

    break;
  }
  *out_n_difat = n_difat;
  *out_n_fat = n_fat;
}

N align(N n, N align) {
  return (n + align - 1) / align;
}

void full_iter(N n_blocks, N* out_n_difat, N* out_n_fat) {
  N n_difat = 0, n_fat = 0;
  while (true) {
    N n_difat_prime = 0;
    if (n_fat > 109)
      n_difat_prime = align(n_fat - 109, 127);
    N n_fat_prime = align((n_blocks + n_fat + n_difat), 128);
    if (n_difat_prime == n_difat && n_fat_prime == n_fat)
      break;
    n_difat = n_difat_prime;
    n_fat = n_fat_prime;
  }
  *out_n_difat = n_difat;
  *out_n_fat = n_fat;
}

void parallel_for(size_t begin,
                  size_t end,
                  std::function<void(size_t, size_t)> body) {
  const size_t num_threads = std::thread::hardware_concurrency();
  std::vector<std::thread> threads;
  const size_t iterations_per_thread = (end - begin) / num_threads;
  for (size_t i = 0; i < num_threads; ++i) {
    size_t thread_begin = begin + i * iterations_per_thread;
    size_t thread_end = std::min(end, begin + (i + 1) * iterations_per_thread);
    threads.push_back(std::thread([thread_begin, thread_end, &body]() {
      body(thread_begin, thread_end);
    }));
  }

  for (std::thread& t : threads)
    t.join();
}

int main() {

  //for (N k = 4000000000; k < 4000001000; ++k) {
  //for (N k = 0; k < 1'000'000; ++k) {
  parallel_for(0, 10'000'000, [](size_t begin, size_t end) {
    for (N k = begin; k != end; ++k) {
      N n_fat_naive, n_difat_naive;
      naive(k, &n_difat_naive, &n_fat_naive);
      N n_fat_full_iter, n_difat_full_iter;
      naive(k, &n_difat_full_iter, &n_fat_full_iter);
      if (n_difat_full_iter != n_difat_naive ||
          n_fat_full_iter != n_fat_naive) {
        printf("%" PRIu64 " %" PRIu64 " %" PRIu64 "\n", k, n_difat_naive,
               n_fat_naive);
        printf("%" PRIu64 " %" PRIu64 " %" PRIu64 "\n", k, n_difat_full_iter,
               n_fat_full_iter);
      }
    }
  });
}
