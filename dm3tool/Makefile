ifneq (,$(filter $(origin CC), undefined default))
  CC = clang
endif

dm3_api.o: dm3_api.c dm3_api.h dm3.h
main.o: main.c dm3_api.h dm3.h

dm3tool: main.o dm3_api.o
	clang -o $@ $^
