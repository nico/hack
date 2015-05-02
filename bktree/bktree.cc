// c++ -std=c++11 -O2 bktree.cc -o bktree

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
using namespace std;

// Returns an empty vector on error.  This is just a toy program.
vector<string> read_dict(const char* file) {
  // C++ I/O tends to be slower than C I/O but reading all 230K words on
  // my system takes only 40ms, so this is good enough here.
  vector<string> dict;
  ifstream input(file);
  for (string line; getline(input, line);)
    dict.push_back(line);
  return dict;
}

int main() {
  vector<string> dict = read_dict("/usr/share/dict/words");
  if (dict.empty())
    return EXIT_FAILURE;

  cout << dict[0] << endl;
}
