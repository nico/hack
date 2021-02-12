.POSIX:
CC     = cc
CFLAGS = -Wall -O2 -std=c17

all: yak
yak: yak.o
	$(CC) -o yak yak.o
yak.o: yak.c
clean:
	rm -f yak yak.o
