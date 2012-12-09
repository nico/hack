/*
  http://www.cs.bell-labs.com/cm/cs/pearls/sec151.html
  $ clang++ -o s151_hash sec151_stlunorderedmap.cpp -O2 -stdlib=libc++
  $ time ./s151_hash < ~/Downloads/bible11.txt > /dev/null 

  real	0m0.424s
  user	0m0.418s
  sys	0m0.005s

  $ clang++ -o s151_hash sec151_stlunorderedmap.cpp -O2
  $ time ./s151_hash < ~/Downloads/bible11.txt > /dev/null 

  real	0m0.294s
  user	0m0.288s
  sys	0m0.005s
 */
#include <iostream>
#include <string>

// hash_map instead of unordered_map to compare to gcc4.2
#pragma clang diagnostic ignored "-W#warnings"
#include <ext/hash_map>
using __gnu_cxx::hash_map;
using namespace std;

namespace __gnu_cxx {
template<>
struct hash<std::string> {
  size_t operator()(const std::string& s) const {
    return hash<const char*>()(s.c_str());
  }
};
}

int main(void)
{   hash_map<string, int> M;
    hash_map<string, int>::iterator j;
    string t;
    while (cin >> t)
        M[t]++;
    for (j = M.begin(); j != M.end(); ++j)
        cout << j->first << " " << j->second << "\n";
    return 0;
}
