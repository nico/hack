// c++ -std=c++11 -O2 bktree.cc -o bktree

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
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
  // array, only two rows are used at a time, so this implementation
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

// See http://blog.notdot.net/2007/4/Damn-Cool-Algorithms-Part-1-BK-Trees
class BkTree {
  const string* value;  // Not owned!
  using Edges = map<int, unique_ptr<BkTree>>;
  Edges children;

 public:
  BkTree(const string* value) : value(value) {}

  void insert(const string* word) {
    int d = edit_distance(*value, *word, /*allow_replacements=*/true, 0);
    const auto& it = children.find(d);
    if (it == children.end())
      children[d] = make_unique<BkTree>(word);
    else
      it->second->insert(word);
  }

  // Prints matches to stdout.
  void query(const string& word, int n, int* count) {
    ++*count;
    int d = edit_distance(*value, word, /*allow_replacements=*/true, 0);
    if (d <= n)
      cout << *value << endl;
    for (auto&& it : children)
      if (d - n <= it.first && it.first <= d + n)
        it.second->query(word, n, count);
  }

  int depth() const {
    int d = 0;
    for (auto&& it : children)
      d = max(d, it.second->depth());
    return d + 1;
  }

  void dump_dot() const {
    for (auto&& it : children) {
      cout << "  " << *value << " -> " << *it.second->value
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
    BkTree index(&words[0]);
    for (size_t i = 1; i < words.size(); ++i)
      index.insert(&words[i]);

    if (dump_dot) {
      cout << "digraph G {" << endl;
      index.dump_dot();
      cout << "}" << endl;
    } else {
      cout << "Index depth: " << index.depth() << " (size: " << words.size()
           << ")" << endl;

      int count = 0;
      index.query(query, n, &count);
      cout << "Queried " << count << " (" << (100 * count / words.size())
           << "%)" << endl;
    }
  } else {
    for (auto&& word : words)
      if (edit_distance(word, query, /*allow_replacements=*/true, n) <= n)
        cout << word << endl;
  }
}
