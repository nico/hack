#include <inttypes.h>
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

int main() {
  int pipe_stdin[2], pipe_stdout[2];
  if (pipe(pipe_stdin) < 0 || pipe(pipe_stdout) < 0) {
    perror("pipe");
    return 1;
  }

  pid_t child_pid = fork();
  if (child_pid < 0) {
    perror("fork");
    return 1;
  }

  if (child_pid == 0) {
    // times-up-again process.
    close(pipe_stdin[1]);
    close(pipe_stdout[0]);
    if (dup2(pipe_stdin[0], STDIN_FILENO) < 0 || 
        dup2(pipe_stdout[1], STDOUT_FILENO) < 0) {
      perror("dup2");
      return 1;
    }
    close(pipe_stdin[0]);
    close(pipe_stdout[1]);
    if (execl("./times-up-again", "./times-up-again", NULL) < 0) {
      perror("execl");
      return 1;
    }
  }

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
