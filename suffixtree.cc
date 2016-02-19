/*
An implementation of Ukkonen's O(n) online suffix tree algorithm,
http://www.cs.helsinki.fi/u/ukkonen/SuffixT1withFigs.pdf

clang -Wall -o suffixtree -O2 suffixtree.cc
 */

#include <err.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>

struct Node {
  Node() {
    memset(next, 0, sizeof(next));
    suffix = 0;
  }

  Node* next[256];
  Node* suffix;
};

int main(int argc, char* argv[]) {
  if (argc < 2)
    errx(EX_USAGE, "usage: %s string [strings...]", argv[0]);

  const char* input = argv[1];

  /* Build suffix trie, algorithm 1 */

  Node bottom;
  Node root;
  for (int i = 0; i < 256; ++i)
    bottom.next[i] = &root;
  root.suffix = &bottom;

  Node* top = &root;
  for (int i = 0, n = strlen(input); i < n; ++i) {
    int ti = input[i];
    Node* r = top;
    Node* oldrp = 0;
    while (!r->next[ti]) {
      Node* rp = new Node;
      r->next[ti] = rp;
      if (r != top)
        oldrp->suffix = rp;
      oldrp = rp;
      r = r->suffix;
    }
    oldrp->suffix = r->next[ti];
    top = top->next[ti];
  }

  /* Build suffix tree. */


  /* Lame application: For each further argument, print if it's a substring
   * of the original string. */
  for (int i = 2; i < argc; ++i) {
    Node* r = &root;
    const char* s = argv[i];
    bool prefix_of_suffix = true;
    for (int j = 0, n = strlen(s); r && j < n; ++j) {
      r = r->next[(int)s[j]];
      if (!r)
        prefix_of_suffix = false;
    }
    if (prefix_of_suffix)
      printf("%s\n", argv[i]);
  }
}
