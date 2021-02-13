// yet another https://viewsourcecode.org/snaptoken/kilo/index.html

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_(q) ((q) & 0x1f)

struct GlobalState {
  int term_rows;
  int term_cols;
  struct termios initial_termios;
};

static struct GlobalState g;

static noreturn void die(const char* s) {
  write(STDOUT_FILENO, "\e[2J", 4);
  write(STDOUT_FILENO, "\e[H", 3);
  perror(s);
  exit(1);
}

static void restoreInitialTermios() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &g.initial_termios) < 0)
    die("reset tcsetattr");
}

static void enterRawMode() {
  if (tcgetattr(STDIN_FILENO, &g.initial_termios) < 0)
    die("tcgetattr");
  struct termios t = g.initial_termios;
  t.c_iflag &= (tcflag_t)~(ICRNL | IXON);
  // FIXME: IEXTEN for Ctrl-V / Ctrl-O on macOS?
  t.c_lflag &= (tcflag_t)~(ECHO | ICANON | ISIG); // Not sure I want ISIG.
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) < 0)
    die("tcsetattr");
  atexit(restoreInitialTermios);
}

static char readKey() {
  char c = 0;
  if (read(STDIN_FILENO, &c, 1) < 0)
    die("read");
  return c;
}

static int getTerminalSize(int* rows, int* cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0)
    return -1;
  if (ws.ws_col == 0) {
    errno = EINVAL;
    return -1;
  }
  *cols = ws.ws_col;
  *rows = ws.ws_row;
  return 0;
}

static void drawRows() {
  for (int y = 0; y < g.term_rows; ++y) {
    for (int x = 0; x < g.term_cols; ++x)
      write(STDOUT_FILENO, u8"░", 3);
    write(STDOUT_FILENO, "\r\n", 2);
  }
}

static void drawScreen() {
  // https://vt100.net/docs/vt100-ug/chapter3.html#ED
  write(STDOUT_FILENO, "\e[2J", 4);
  // https://vt100.net/docs/vt100-ug/chapter3.html#CUP
  write(STDOUT_FILENO, "\e[H", 3);

  drawRows();

  write(STDOUT_FILENO, "\e[H", 3);
}

static void processKey() {
  char c = readKey();
  switch (c) {
    case CTRL_('q'):
      write(STDOUT_FILENO, "\e[2J", 4);
      write(STDOUT_FILENO, "\e[H", 3);
      exit(0);
      break;
  }
}

static void init() {
  if (getTerminalSize(&g.term_rows, &g.term_cols) < 0)
    die("getTerminalSize");
}

int main() {
  enterRawMode();
  init();
  while (1) {
    drawScreen();
    processKey();
  }
}
