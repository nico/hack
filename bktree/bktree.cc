// c++ -std=c++11 -O2 bktree.cc -o bktree

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
using namespace std;

// Returns an empty vector on error.  This is just a toy program.
vector<string> read_words(const char* file) {
  ifstream input(file);
  return vector<string>(istream_iterator<string>(input),
                        istream_iterator<string>());
}

int main() {
  vector<string> dict = read_words("/usr/share/dict/words");
  if (dict.empty())
    return EXIT_FAILURE;

  cout << dict[0] << endl;
}
