#include <errno.h>
#include <inttypes.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int64_t eval(char** c) {
  int64_t res;

  if (**c != '(') return -1;
  *c += 1;

  if (**c != '(') {
    res = strtol(*c, c, 10);
  } else {
    int64_t lhs = eval(c);
    char op = (*c)[1];
    *c += 3;
    int64_t rhs = eval(c);
    switch (op) {
      case '+': res = lhs + rhs; break;
      case '-': res = lhs - rhs; break;
      case '*': res = lhs * rhs; break;
    }
  }

  if (**c != ')') return -1;
  *c += 1;
  return res;
}

char **environ;
#define S(x) { int t = x; if (t != 0) { errno = t; perror(#x); return 1; } }

int main() {
  int pipe_stdin[2], pipe_stdout[2];
  if (pipe(pipe_stdin) < 0 || pipe(pipe_stdout) < 0) {
    perror("pipe");
    return 1;
  }

  // cf `man posix_spawn`
  posix_spawn_file_actions_t action;
  S(posix_spawn_file_actions_init(&action));
  S(posix_spawn_file_actions_adddup2(&action, pipe_stdin[0], STDIN_FILENO));
  S(posix_spawn_file_actions_adddup2(&action, pipe_stdout[1], STDOUT_FILENO));
  S(posix_spawn_file_actions_addclose(&action, pipe_stdin[0]));
  S(posix_spawn_file_actions_addclose(&action, pipe_stdin[1]));
  S(posix_spawn_file_actions_addclose(&action, pipe_stdout[0]));
  S(posix_spawn_file_actions_addclose(&action, pipe_stdout[1]));

  pid_t pid;
  char* args[] = { "./times-up-again", NULL };
  S(posix_spawn(&pid, "./times-up-again", &action, NULL, args, environ));
  S(posix_spawn_file_actions_destroy(&action));

  close(pipe_stdin[0]);
  close(pipe_stdout[1]);

  // Read stdout from executable.
  char buf[512];
  memset(buf, 0, sizeof(buf));
  if (read(pipe_stdout[0], buf, sizeof(buf)) < 0) {
    perror("read");
    return 1;
  }
  if (!strchr(buf, '\n')) return 1;

  // Evaluate. Challenge assumes int64_t overflow.
  char* eval_in = buf + strlen("Challenge: ");
  int64_t res = eval(&eval_in);

  // Write result to child.
  snprintf(buf, sizeof(buf), "%" PRId64 "\n", res);
  if (write(pipe_stdin[1], buf, strlen(buf)) < 0) {
    perror("write");
    return 1;
  }

  // ...and echo child output.
  int n;
  while ((n = read(pipe_stdout[0], buf, sizeof(buf))) > 0)
    printf("%.*s", n, buf);
  printf("\n");
}
