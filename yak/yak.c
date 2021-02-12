// yet another https://viewsourcecode.org/snaptoken/kilo/index.html

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define CTRL(q) ((q) & 0x1f)

struct termios g_initial_termios;

static void restoreInitialTermios() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_initial_termios) < 0)
    perror("reset tcsetattr");
}

static void enterRawMode() {
  if (tcgetattr(STDIN_FILENO, &g_initial_termios) < 0) {
    perror("tcgetattr");
    return;
  }
  struct termios t = g_initial_termios;
  t.c_iflag &= (tcflag_t)~(ICRNL | IXON);
  // FIXME: IEXTEN for Ctrl-V / Ctrl-O on macOS?
  t.c_lflag &= (tcflag_t)~(ECHO | ICANON | ISIG); // Not sure I want ISIG.
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) < 0) {
    perror("tcsetattr");
    return;
  }
  atexit(restoreInitialTermios);
}

static char readKey() {
  char c = 0;
  if (read(STDIN_FILENO, &c, 1) < 0)
    perror("read");
  return c;
}

void drawScreen() {
  // https://vt100.net/docs/vt100-ug/chapter3.html#ED
  write(STDOUT_FILENO, "\e[2J", 4);
  // https://vt100.net/docs/vt100-ug/chapter3.html#CUP
  write(STDOUT_FILENO, "\e[H", 3);
}

static void processKey() {
  char c = readKey();
  switch (c) {
    case CTRL('q'):
      exit(0);
      break;
  }
}

int main() {
  enterRawMode();
  while (1) {
    drawScreen();
    processKey();
  }
}
