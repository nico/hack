/*
   $ clang++ -o s151_map sec151_stlmap.cpp -O2 -stdlib=libc++
   $ time ./s151_map < ~/Downloads/bible11.txt > /dev/null

   real	0m0.557s
   user	0m0.548s
   sys	0m0.005s

   $ clang++ -o s151_map sec151_stlmap.cpp -O2
   $ time ./s151_map < ~/Downloads/bible11.txt > /dev/null

   real	0m0.403s
   user	0m0.396s
   sys	0m0.006s

   => http://llvm.org/PR14554 :-/
 */
#include <iostream>
#include <map>
#include <string>
using namespace std;

int main(void)
{   map<string, int> M;
    map<string, int>::iterator j;
    string t;
    while (cin >> t)
        M[t]++;
    for (j = M.begin(); j != M.end(); ++j)
        cout << j->first << " " << j->second << "\n";
    return 0;
}
