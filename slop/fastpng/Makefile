CC ?= clang
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

demo: demo.c fastpng.c fastpng.h
	$(CC) $(CFLAGS) demo.c fastpng.c -o $@

clean:
	rm -f demo

.PHONY: clean
