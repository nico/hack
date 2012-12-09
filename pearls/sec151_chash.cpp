/*
  http://www.cs.bell-labs.com/cm/cs/pearls/sec151.html
  $ clang -o s151_c sec151_chash.cpp -O2
  $ time ./s151_c < ~/Downloads/bible11.txt > /dev/null

  real	0m0.082s
  user	0m0.076s
  sys	0m0.003s
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct node *nodeptr;
typedef struct node {
    char *word;
    int count;
    nodeptr next;
} node;

#define NHASH 29989
#define MULT 31
nodeptr bin[NHASH];

unsigned int hash(char *p) {
    unsigned int h = 0;
    for ( ; *p; p++)
        h = MULT * h + *p;
    return h % NHASH;
}

void incword(char *s) {
    nodeptr p;
    unsigned int h = hash(s);
    for (p = bin[h]; p != NULL; p = p->next)
        if (strcmp(s, p->word) == 0) {
            (p->count)++;
            return;
        }
    p = (node*)malloc(sizeof(node));
    p->count = 1;
    p->word = (char*)malloc(strlen(s)+1);
    strcpy(p->word, s);
    p->next = bin[h];
    bin[h] = p;
}

int main(void) {
    char buf[1024];
    for (int i = 0; i < NHASH; ++i)
        bin[i] = NULL;
    while (scanf("%s", buf) != EOF)
        incword(buf);
    for (int i = 0; i < NHASH; ++i)
        for (nodeptr p = bin[i]; p != NULL; p = p->next)
            printf("%s %u\n", p->word, p->count);
    return 0;
}
