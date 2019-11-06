mkdir build
cd build
clang-cl /Zi -no-canonical-prefixes -fdebug-compilation-dir . -c ..\file.cc
clang-cl /Zi -no-canonical-prefixes -fdebug-compilation-dir . -c ..\subdir\file2.cc
lld-link /debug /pdbaltpath:%_PDB% /pdbsourcepath:o:\ file.obj file2.obj
cd ..
