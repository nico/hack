// c++ -std=c++11 -O2 bktree.cc -o bktree

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
using namespace std;

namespace {

int edit_distance(const string& s1,
                  const string& s2,
                  bool allow_replacements,
                  int max_edit_distance) {
  // The algorithm implemented below is the "classic"
  // dynamic-programming algorithm for computing the Levenshtein
  // distance, which is described here:
  //
  //   http://en.wikipedia.org/wiki/Levenshtein_distance
  //
  // Although the algorithm is typically described using an m x n
  // array, only two rows are used at a time, so this implemenation
  // just keeps two separate vectors for those two rows.
  int m = s1.size();
  int n = s2.size();

  vector<int> previous(n + 1);
  vector<int> current(n + 1);

  for (int i = 0; i <= n; ++i)
    previous[i] = i;

  for (int y = 1; y <= m; ++y) {
    current[0] = y;
    int best_this_row = current[0];

    for (int x = 1; x <= n; ++x) {
      if (allow_replacements) {
        current[x] = min(previous[x - 1] + (s1[y - 1] == s2[x - 1] ? 0 : 1),
                         min(current[x - 1], previous[x]) + 1);
      } else {
        if (s1[y - 1] == s2[x - 1])
          current[x] = previous[x - 1];
        else
          current[x] = min(current[x - 1], previous[x]) + 1;
      }
      best_this_row = min(best_this_row, current[x]);
    }

    if (max_edit_distance && best_this_row > max_edit_distance)
      return max_edit_distance + 1;

    current.swap(previous);
  }

  return previous[n];
}

// Returns an empty vector on error.  This is just a toy program.
vector<string> read_words(const char* file) {
  ifstream input(file);
  return vector<string>(istream_iterator<string>(input),
                        istream_iterator<string>());
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2 || argc > 3) {
    cerr << "Usage: bktree [n] query" << endl;
    return EXIT_FAILURE;
  }

  int n = argc == 3 ? atoi(argv[1]) : 2;
  string query(argv[argc - 1]);

  vector<string> words = read_words("/usr/share/dict/words");
  if (words.empty())
    return EXIT_FAILURE;

  for (auto&& word : words)
    if (edit_distance(word, query, /*allow_replacements=*/true, n) <= n)
      cout << word << endl;
}
