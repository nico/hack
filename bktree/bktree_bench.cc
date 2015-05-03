// c++ -std=c++1y -O2 bktree_bench.cc -o bktree_bench

// Benchmark tests for bktree.cc
//
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

int edit_distance(const string& s1, const string& s2) {
  int m = s1.size();
  int n = s2.size();

  int storage[2*(n + 1)];
  int* previous = storage;
  int* current = previous + n + 1;

  for (int i = 0; i <= n; ++i)
    previous[i] = i;

  for (int y = 1; y <= m; ++y) {
    current[0] = y;
    for (int x = 1; x <= n; ++x) {
      current[x] = min(previous[x - 1] + (s1[y - 1] == s2[x - 1] ? 0 : 1),
                       min(current[x - 1], previous[x]) + 1);
    }

    swap(previous, current);
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
    int d = edit_distance(*value, *word);
    const auto& it = children.lower_bound(d);
    if (it != children.end() && it->first == d)
      it->second->insert(word);
    else
      children.insert(it, Edges::value_type(d, make_unique<BkTree>(word)));
  }

  int depth() const {
    int d = 0;
    for (auto&& it : children)
      d = max(d, it.second->depth());
    return d + 1;
  }
};

}  // namespace

int main(int argc, char* argv[]) {
  const char* wordfile = "/usr/share/dict/words";
  for (; argc > 1 && argv[1][0] == '-'; ++argv, --argc) {
    if(strcmp(argv[1], "-w") == 0) {
      if (argc < 3) {
        cerr << "-w needs wordfile argument" << endl;
        return EXIT_FAILURE;
      }
      wordfile = argv[2];
      ++argv;
      --argc;
    }
  }

  const vector<string> words = read_words(wordfile);
  if (words.empty())
    return EXIT_FAILURE;

  auto start_time = chrono::high_resolution_clock::now();
  BkTree index(&words[0]);
  for (size_t i = 1; i < words.size(); ++i)
    index.insert(&words[i]);
  auto end_time = chrono::high_resolution_clock::now();
  cout << "Index construction took "
       << chrono::duration_cast<chrono::milliseconds>(end_time - start_time)
              .count() << "ms" << endl;

  cout << "Index depth: " << index.depth() << " (size: " << words.size()
       << ")" << endl;
}
