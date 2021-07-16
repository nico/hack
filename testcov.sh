#!/bin/bash

rm -rf profiles report

echo 'int main() {}' > main.c

out/gn/bin/clang \
  -fcoverage-mapping \
  -fprofile-instr-generate=$PWD/profiles/%4m.profraw \
  -c main.c

out/gn/bin/clang -fprofile-instr-generate main.o -fuse-ld=lld

./a.out

llvm/utils/prepare-code-coverage-artifact.py \
  out/gn/bin/llvm-profdata \
  out/gn/bin/llvm-cov \
  profiles/ report/ \
  a.out
