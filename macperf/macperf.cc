/*
clang++ macperf.cc -o macperf -Wall -Wextra -Wconversion \
    -F /System/Library/PrivateFrameworks \
    -framework kperfdata

*/

// Based on https://gist.github.com/ibireme/173517c208c7dc333ba962c1f0d67d12

#include "kperf.h"
#include "kperfdata.h"
#include <stdio.h>

int main(int argc, char* argv[]) {
  int ret;
  kpep_db *db = NULL;
  if ((ret = kpep_db_create(NULL, &db))) {
      printf("Error: cannot load pmc database: %d.\n", ret);
      return 1;
  }
  printf("loaded db: %s (%s)\n", db->name, db->marketing_name);
  printf("number of fixed counters: %zu\n", db->fixed_counter_count);
  printf("number of configurable counters: %zu\n", db->config_counter_count);
}
