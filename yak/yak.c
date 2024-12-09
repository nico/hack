// yet another https://viewsourcecode.org/snaptoken/kilo/index.html

#define _DEFAULT_SOURCE // For cfmakeraw() in termios.h

#if defined(__linux__)
// For getline() in stdio.h.
// Defining this on macOS makes termios.h not declare cfmakeraw().
#define _POSIX_C_SOURCE 200809L
#endif

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_(q) ((q) & 0x1f)

enum editorKey {
  ARROW_UP = 1000,
  ARROW_DOWN,
  ARROW_RIGHT,
  ARROW_LEFT,
  DELETE,
};

struct Row {
  size_t size;
  char* chars;
};

struct GlobalState {
  // Cursor position in screen space. (0, 0) is always the upper left corner
  // of what's visible on screen, independent of scrolling.
  size_t cx, cy;

  // Index of the top-left cell drawn on the screen.
  size_t topmost_line;
  size_t leftmost_column;

  size_t term_rows;
  size_t term_cols;
  size_t num_rows;
  struct Row* rows;
  char* filename;
  struct termios initial_termios;
};

static struct GlobalState g;

static volatile sig_atomic_t g_is_window_size_stale = 0;
static void handle_sigwinch() { g_is_window_size_stale = 1; }

static noreturn void die(const char* s) {
  // https://vt100.net/docs/vt100-ug/chapter3.html#ED
  write(STDOUT_FILENO, "\e[2J", 4);
  // https://vt100.net/docs/vt100-ug/chapter3.html#CUP
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
  // Make read() timeout every 10 ms, to make it nonblocking
  // (in case we want to show an animation or similar).
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 1;  // In 1/10th of a second.
#endif

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) < 0)
    die("tcsetattr");
  atexit(restoreInitialTermios);
}

static int readKey() {
  int c = 0;
  if (read(STDIN_FILENO, &c, 1) < 0) {
    // Happens e.g. if the window is resized, due to the SIGWINCH handler.
    if (errno == EINTR)
      return -1;
    die("read");
  }

  if (c == '\e') {
    // Might be start of an esc sequence, or might just be the esc key.
    // Set input to nonblocking and see if there are more characters.
    struct termios cur_termios;
    if (tcgetattr(STDIN_FILENO, &cur_termios) < 0)
      die("tcgetattr");
    struct termios t = cur_termios;
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) < 0)
      die("tcsetattr");

    int nseq = 0;
    char seq[3];
    if (read(STDIN_FILENO, &seq[nseq], 1) == 1) ++nseq;
    if (read(STDIN_FILENO, &seq[nseq], 1) == 1) ++nseq;

    if (nseq == 2 && seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[nseq], 1) == 1) ++nseq;
        if (nseq == 3 && seq[2] == '~') {
          switch (seq[1]) {
            case '3': c = DELETE; break;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': c = ARROW_UP; break;
          case 'B': c = ARROW_DOWN; break;
          case 'C': c = ARROW_RIGHT; break;
          case 'D': c = ARROW_LEFT; break;
        }
      }
    }

    if (tcsetattr(STDIN_FILENO, TCSANOW, &cur_termios) < 0)
      die("tcsetattr");
  }

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

static int getTerminalSize(size_t* rows, size_t* cols) {
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

static void editorAppendRow(const char* line, size_t line_len) {
  g.rows = realloc(g.rows, sizeof(struct Row) * (g.num_rows + 1));

  size_t i = g.num_rows;
  g.rows[i].size = (size_t)line_len;
  g.rows[i].chars = malloc((size_t)line_len + 1);
  memcpy(g.rows[i].chars, line, line_len);
  g.rows[i].chars[line_len] = '\0';
  g.num_rows++;
}

static void editorOpen(const char* filename) {
  FILE* f = fopen(filename, "r");
  if (!f)
    die("fopen");

  free(g.filename);
  g.filename = strdup(filename);

  char *line = NULL;
  size_t line_cap = 0;
  ssize_t line_len;
  while ((line_len = getline(&line, &line_cap, f)) > 0) {
    // FIXME: This should be handled at display time, not at load time.
    while (line_len > 0 && (line[line_len - 1] == '\n' ||
                            line[line_len - 1] == '\r'))
      --line_len;
    editorAppendRow(line, (size_t)line_len);
  }
  if (line)
    free(line);
  fclose(f);
}

static void drawRows(struct abuf* ab) {
  for (size_t y = 0; y < g.term_rows; ++y) {
    size_t file_row = y + g.topmost_line;
    if (file_row < g.num_rows) {
      ssize_t len = (ssize_t)g.rows[file_row].size - (ssize_t)g.leftmost_column;
      if (len < 0) len = 0;
      if (len > (ssize_t)g.term_cols) len = (ssize_t)g.term_cols;
      abAppend(ab, g.rows[file_row].chars + g.leftmost_column, (size_t)len);
      // \e[K to clear rest of line if not drawing whole line
      // https://vt100.net/docs/vt100-ug/chapter3.html#EL
      if ((size_t)len < g.term_cols)
        abAppend(ab, "\e[K", 3);
    } else {
      for (size_t x = 0; x < g.term_cols; ++x)
        abAppend(ab, u8"░", 3);
    }
    abAppend(ab, "\r\n", 2);
  }
}

static void drawStatusBar(struct abuf* ab) {
  // https://vt100.net/docs/vt100-ug/chapter3.html#SGR
  abAppend(ab, "\e[7m", 4);
  char status[256], rstatus[80];
  snprintf(status, sizeof(status), "%.20s",
      g.filename ? g.filename : "[unnamed]");
  snprintf(rstatus, sizeof(rstatus), "%zu/%zu", g.cy + g.topmost_line + 1,
      g.num_rows);
  size_t x = strlen(status);
  if (x > g.term_cols)
    x = g.term_cols;
  abAppend(ab, status, x);
  for (; x < g.term_cols; ++x) {
    size_t rlen = strlen(rstatus);
    if (x + rlen == g.term_cols) {
      abAppend(ab, rstatus, rlen);
      break;
    }
    abAppend(ab, " ", 1);
  }
  abAppend(ab, "\e[m", 4);
}

static void positionCursor(struct abuf* ab, size_t x, size_t y) {
  // https://vt100.net/docs/vt100-ug/chapter3.html#CUP
  if (x == 0 && y == 0) {
    // Optimization: Send shorter code for top left.
    abAppend(ab, "\e[H", 3);
    return;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "\e[%zu;%zuH", y + 1, x + 1);
  abAppend(ab, buf, strlen(buf));
}

// https://vt100.net/docs/vt510-rm/DECTCEM.html
static void hideCursor(struct abuf* ab) { abAppend(ab, "\e[?25l", 6); }
static void showCursor(struct abuf* ab) { abAppend(ab, "\e[?25h", 6); }

static void drawScreen() {
  struct abuf ab = ABUF_INIT;

  hideCursor(&ab);
  positionCursor(&ab, 0, 0);
  drawRows(&ab);
  drawStatusBar(&ab);

  positionCursor(&ab, g.cx, g.cy);
  showCursor(&ab);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

static void put_cursor_at_end_of_line() {
  g.cx = g.rows[g.topmost_line + g.cy].size - g.leftmost_column;
  if (g.cx >= g.term_cols) {
    g.cx = g.term_cols;
    g.leftmost_column = g.rows[g.topmost_line + g.cy].size - g.term_cols;
  }
  if (g.cx)
    g.cx--;
}

static void keep_cursor_in_bounds_after_vertical_move() {
  size_t length_of_current_line = g.rows[g.topmost_line + g.cy].size;
  if (g.cx + g.leftmost_column >= length_of_current_line) {
    if (g.leftmost_column > length_of_current_line) {
      g.leftmost_column = length_of_current_line;
      if (g.leftmost_column)
        --g.leftmost_column;
    }
    put_cursor_at_end_of_line();
  }
}

static void moveCursor(char key) {
  size_t line = g.topmost_line + g.cy;
  switch (key) {
    case 'h':
      if (g.cx > 0)
        g.cx--;
      else if (g.leftmost_column > 0)
        g.leftmost_column--;
      break;
    case 'j':
      if (g.cy + 1 < g.term_rows) {
        if (g.topmost_line + g.cy + 1 < g.num_rows)
          g.cy++;
      } else if (g.topmost_line + g.term_rows < g.num_rows)
        g.topmost_line++;
      break;
    case 'k':
      if (g.cy > 0)
        g.cy--;
      else if (g.topmost_line > 0)
        g.topmost_line--;
      break;
    case 'l':
      if (g.num_rows) {
        if (g.cx + 1 < g.term_cols) {
          if (g.cx + 1 < g.rows[line].size)
            g.cx++;
        } else if (g.leftmost_column + g.term_cols < g.rows[line].size)
          g.leftmost_column++;
      }
      break;

    case 'g':
      g.cy = 0;
      g.topmost_line = 0;
      break;
    case 'G':
      if (g.num_rows > g.term_rows) {
        g.cy = g.term_rows - 1;
        g.topmost_line = g.num_rows - g.term_rows;
      } else if (g.num_rows) {
        g.cy = g.num_rows - 1;
      }
      break;
    case '0':
      g.cx = 0;
      g.leftmost_column = 0;
      break;
    case '$':
      if (g.num_rows)
        put_cursor_at_end_of_line();
      break;
  }

  bool did_scroll_vertically = g.topmost_line + g.cy != line;
  if (did_scroll_vertically)
    keep_cursor_in_bounds_after_vertical_move();
}

static void processKey() {
  int c = readKey();
  switch (c) {
    case 'q':
    case CTRL_('q'):
      // https://vt100.net/docs/vt100-ug/chapter3.html#ED
      write(STDOUT_FILENO, "\e[2J", 4);
      // https://vt100.net/docs/vt100-ug/chapter3.html#CUP
      write(STDOUT_FILENO, "\e[H", 3);
      exit(0);
      break;

    case CTRL_('y'): {
      size_t line = g.topmost_line + g.cy;

      if (g.topmost_line >= 1) {
        g.topmost_line -= 1;
        if (g.cy < g.term_rows - 1)
          g.cy += 1;
      }

      if (line != g.topmost_line + g.cy)
        keep_cursor_in_bounds_after_vertical_move();
      break;
    }

    case CTRL_('e'): {
      if (g.num_rows == 0)
        break;

      size_t line = g.topmost_line + g.cy;

      if (g.topmost_line < g.num_rows - 1) {
        g.topmost_line += 1;
        if (g.cy > 0)
          g.cy -= 1;
      }

      if (g.topmost_line + g.cy >= g.num_rows)
        g.cy = g.num_rows - g.topmost_line - 1;

      if (line != g.topmost_line + g.cy)
        keep_cursor_in_bounds_after_vertical_move();
      break;
    }

    case CTRL_('b'): {
      size_t line = g.topmost_line + g.cy;

      if (g.topmost_line > g.term_rows)
        g.topmost_line -= g.term_rows;
      else if (g.topmost_line > 0)
        g.topmost_line = 0;
      else if (g.cy > 0)
        g.cy = 0;

      if (line != g.topmost_line + g.cy)
        keep_cursor_in_bounds_after_vertical_move();
      break;
    }

    case CTRL_('f'): {
      if (g.num_rows == 0)
        break;

      size_t line = g.topmost_line + g.cy;

      if (g.topmost_line + g.term_rows < g.num_rows - 1)
        g.topmost_line += g.term_rows;
      else if (g.topmost_line < g.num_rows - 1)
        g.topmost_line = g.num_rows - 1;

      if (g.topmost_line + g.cy >= g.num_rows)
        g.cy = g.num_rows - g.topmost_line - 1;

      if (line != g.topmost_line + g.cy)
        keep_cursor_in_bounds_after_vertical_move();
      break;
    }

    case 'g':
    case 'G':
    case '0':
    case '$':
      moveCursor((char)c);
      break;

    case 'h':
    case ARROW_LEFT:
      moveCursor('h');
      break;
    case 'j':
    case ARROW_DOWN:
      moveCursor('j');
      break;
    case 'k':
    case ARROW_UP:
      moveCursor('k');
      break;
    case 'l':
    case ARROW_RIGHT:
      moveCursor('l');
      break;
  }
}

static void refreshWindowSize() {
  if (!g_is_window_size_stale)
    return;
  g_is_window_size_stale = 0;
  if (getTerminalSize(&g.term_rows, &g.term_cols) < 0)
    die("getTerminalSize");
  g.term_rows--;
}

static void init() {
  g.cx = 0;
  g.cy = 0;
  g.topmost_line = 0;
  g.leftmost_column = 0;
  g.num_rows = 0;
  g.rows = NULL;
  g.filename = NULL;

  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = handle_sigwinch;
  if (sigaction(SIGWINCH, &act, NULL) < 0)
    die("sigaction");

  raise(SIGWINCH);  // Get initial window size.
}

int main(int argc, char* argv[]) {
  enterRawMode();
  init();
  if (argc >= 2)
    editorOpen(argv[1]);

  while (1) {
    refreshWindowSize();
    drawScreen();
    processKey();
  }
}
