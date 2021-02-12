// yet another https://viewsourcecode.org/snaptoken/kilo/index.html

#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios g_initial_termios;

static void resetEcho() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_initial_termios) < 0)
    perror("reset tcsetattr");
}

static void disableEcho() {
  if (tcgetattr(STDIN_FILENO, &g_initial_termios) < 0) {
    perror("tcgetattr");
    return;
  }
  struct termios t = g_initial_termios;
  t.c_lflag &= (tcflag_t)~ECHO;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) < 0) {
    perror("tcsetattr");
    return;
  }
  atexit(resetEcho);
}

int main() {
  disableEcho();

  char c;
  while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q')
    ;
}
