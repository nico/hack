// c++ -std=c++1y -O2 bktree.cc -o bktree

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
using namespace std;

namespace {

struct StringRef {
  StringRef(const string& s) : s_(s.data()), n_(s.size()) {}
  const char* s_;
  size_t n_;

  string AsString() const { return string(s_, n_); }
};

// The algorithm implemented below is the "classic"
// dynamic-programming algorithm for computing the Levenshtein
// distance, which is described here:
//
//   http://en.wikipedia.org/wiki/Levenshtein_distance
//
// Although the algorithm is typically described using an m x n
// array, only one row plus one element are used at a time, so this
// implementation just keeps one vector for the row.  To update one entry,
// only the entries to the left, top, and top-left are needed.  The left
// entry is in Row[x-1], the top entry is what's in Row[x] from the last
// iteration, and the top-left entry is stored in Previous.
int edit_distance(const StringRef& s1, const StringRef& s2) {
  int m = s1.n_, n = s2.n_;

  int row[n + 1];
  for (int i = 1; i <= n; ++i)
    row[i] = i;

  for (int y = 1; y <= m; ++y) {
    row[0] = y;
    for (int x = 1, previous = y - 1; x <= n; ++x) {
      int old_row = row[x];
      row[x] = min(previous + (s1.s_[y - 1] == s2.s_[x - 1] ? 0 : 1),
                   min(row[x - 1], row[x]) + 1);  // row[x] is from last round.
      previous = old_row;
    }
  }

  return row[n];
}

// Same, but with an early exit given an upper bound for the result.
int edit_distance_bound(StringRef s1, StringRef s2, int upper_bound) {
  int m = s1.n_, n = s2.n_;

  int row[n + 1];
  for (int i = 1; i <= n; ++i)
    row[i] = i;

  for (int y = 1; y <= m; ++y) {
    int best_this_row = row[0] = y;
    for (int x = 1, previous = y - 1; x <= n; ++x) {
      int old_row = row[x];
      row[x] = min(previous + (s1.s_[y - 1] == s2.s_[x - 1] ? 0 : 1),
                   min(row[x - 1], row[x]) + 1);  // row[x] is from last round.
      previous = old_row;
      best_this_row = min(best_this_row, row[x]);
    }
    if (best_this_row > upper_bound)
      return upper_bound + 1;
  }

  return row[n];
}

// Returns an empty vector on error.  This is just a toy program.
vector<string> read_words(const char* file) {
  ifstream input(file);
  return vector<string>(istream_iterator<string>(input),
                        istream_iterator<string>());
}

// See http://blog.notdot.net/2007/4/Damn-Cool-Algorithms-Part-1-BK-Trees
class BkTree {
  StringRef value;
  using Edges = map<int, unique_ptr<BkTree>>;
  Edges children;

 public:
  BkTree(StringRef value) : value(value) {}

  void insert(StringRef word) {
    int d = edit_distance(value, word);
    const auto& it = children.find(d);
    if (it == children.end())
      children[d] = make_unique<BkTree>(word);
    else
      it->second->insert(word);
  }

  // Prints matches to stdout.
  void query(StringRef word, int n, int* count) {
    ++*count;
    int d = edit_distance(value, word);
    if (d <= n)
      cout << value.AsString() << endl;
    for (auto&& it = children.lower_bound(d - n),
             && end = children.upper_bound(d + n);
         it != end; ++it) {
      it->second->query(word, n, count);
    }
  }

  int depth() const {
    int d = 0;
    for (auto&& it : children)
      d = max(d, it.second->depth());
    return d + 1;
  }

  void dump_dot() const {
    for (auto&& it : children) {
      cout << "  " << value.AsString() << " -> " << it.second->value.AsString()
           << " [label=\"" << it.first << "\"];" << endl;
    }
    for (auto&& it : children)
      it.second->dump_dot();
  }
};

}  // namespace

int main(int argc, char* argv[]) {
  bool use_index = true;
  bool dump_dot = false;
  const char* wordfile = "/usr/share/dict/words";
  for (; argc > 1 && argv[1][0] == '-'; ++argv, --argc) {
    if (strcmp(argv[1], "-b") == 0)  // Brute force mode
      use_index = false;
    else if(strcmp(argv[1], "-dot") == 0)
      dump_dot = true;
    else if(strcmp(argv[1], "-w") == 0) {
      if (argc < 3) {
        cerr << "-w needs wordfile argument" << endl;
        return EXIT_FAILURE;
      }
      wordfile = argv[2];
      ++argv;
      --argc;
    }
  }

  if (argc < 2 || argc > 3) {
    cerr << "Usage: bktree [-w wordfile] [-dot] [-b] [n] query" << endl;
    return EXIT_FAILURE;
  }

  int n = argc == 3 ? atoi(argv[1]) : 2;
  string query(argv[argc - 1]);

  const vector<string> words = read_words(wordfile);
  if (words.empty())
    return EXIT_FAILURE;

  if (use_index) {
    auto start_time = chrono::high_resolution_clock::now();
    BkTree index(words[0]);
    for (size_t i = 1; i < words.size(); ++i)
      index.insert(words[i]);
    auto end_time = chrono::high_resolution_clock::now();

    if (dump_dot) {
      cout << "digraph G {" << endl;
      index.dump_dot();
      cout << "}" << endl;
    } else {
      cout << "Index construction took "
           << chrono::duration_cast<chrono::milliseconds>(end_time - start_time)
                  .count() << "ms" << endl;
      cout << "Index depth: " << index.depth() << " (size: " << words.size()
           << ")" << endl;

      int count = 0;
      auto start_time = chrono::high_resolution_clock::now();
      index.query(query, n, &count);
      auto end_time = chrono::high_resolution_clock::now();
      cout << "Indexed query took "
           << chrono::duration_cast<chrono::milliseconds>(end_time - start_time)
                  .count() << "ms" << endl;
      cout << "Queried " << count << " (" << (100 * count / words.size())
           << "%)" << endl;
    }
  } else {
    auto start_time = chrono::high_resolution_clock::now();
    for (auto&& word : words) {
      if (edit_distance_bound(word, query, n) <= n)
        cout << word << endl;
    }
    auto end_time = chrono::high_resolution_clock::now();
    cout << "Brute force query took "
         << chrono::duration_cast<chrono::milliseconds>(end_time - start_time)
                .count() << "ms" << endl;
  }
}
