clang-cl -no-canonical-prefixes -fdebug-compilation-dir . -c file.cc
lld-link /debug /pdbsourcepath:o:\ /pdbaltpath:%_PDB%
