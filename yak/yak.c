// yet another https://viewsourcecode.org/snaptoken/kilo/index.html

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
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
  cfmakeraw(&t);

#if 0
  // Make read timeout every 10 ms.
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 1;
#endif

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

struct abuf {
  size_t len;
  char* b;
};

#define ABUF_INIT {0, NULL}

void abAppend(struct abuf* ab, const char* s, size_t len) {
  char* newbuf = realloc(ab->b, ab->len + len);
  if (!newbuf)
    return;
  memcpy(&newbuf[ab->len], s, len);
  ab->b = newbuf;
  ab->len += len;
}

void abFree(struct abuf* ab) {
  free(ab->b);
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

static void drawRows(struct abuf* ab) {
  for (int y = 0; y < g.term_rows; ++y) {
    for (int x = 0; x < g.term_cols; ++x)
      abAppend(ab, u8"░", 3);
    // FIXME: \e[K to clear rest of line if not drawing whole line
    // https://vt100.net/docs/vt100-ug/chapter3.html#EL
    if (y != g.term_rows - 1)
      abAppend(ab, "\r\n", 2);
  }
}

static void drawScreen() {
  struct abuf ab = ABUF_INIT;

  // https://vt100.net/docs/vt510-rm/DECTCEM.html
  abAppend(&ab, "\e[?25l", 6);
  // https://vt100.net/docs/vt100-ug/chapter3.html#CUP
  abAppend(&ab, "\e[H", 3);

  drawRows(&ab);

  abAppend(&ab, "\e[H", 3);
  abAppend(&ab, "\e[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
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
