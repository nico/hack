/*
   $ clang++ -o s151_hash sec151_stlunorderedmap.cpp -O2 -stdlib=libc++
   $ time ./s151_hash < ~/Downloads/bible11.txt > /dev/null 

   real	0m0.424s
   user	0m0.418s
   sys	0m0.005s
 */
#include <iostream>
#include <string>
#include <unordered_map>
using namespace std;

int main(void)
{   unordered_map<string, int> M;
    unordered_map<string, int>::iterator j;
    string t;
    while (cin >> t)
        M[t]++;
    for (j = M.begin(); j != M.end(); ++j)
        cout << j->first << " " << j->second << "\n";
    return 0;
}
