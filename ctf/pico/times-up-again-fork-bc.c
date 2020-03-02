#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main() {
  // Start bc first, so times-up-again doesn't count its process creation time.
  int bc_stdin[2], bc_stdout[2];
  if (pipe(bc_stdin) < 0 || pipe(bc_stdout) < 0) {
    perror("pipe");
    return 1;
  }

  pid_t bc_pid = fork();
  if (bc_pid < 0) {
    perror("fork");
    return 1;
  }

  if (bc_pid == 0) {
    // bc process.
    close(bc_stdin[1]);
    close(bc_stdout[0]);
    if (dup2(bc_stdin[0], STDIN_FILENO) < 0 || 
        dup2(bc_stdout[1], STDOUT_FILENO) < 0) {
      perror("dup2");
      return 1;
    }
    close(bc_stdin[0]);
    close(bc_stdout[1]);
    if (execlp("bc", "bc", NULL) < 0) {
      perror("execl");
      return 1;
    }
  }

  close(bc_stdin[0]);
  close(bc_stdout[1]);
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
  int n = read(pipe_stdout[0], buf, sizeof(buf));
  if (n < 0) {
    perror("read");
    return 1;
  }
  char* eol = strchr(buf, '\n');
  if (!eol)
    return 1;
  eol[1] = '\0';

  // Pipe it into bc.

  char* bc_in = buf + strlen("Challenge: ");
  char bc_in_buf[512];
  strcpy(bc_in_buf, bc_in);
  //printf("sending to bc: %s\n", bc_in);
  if (write(bc_stdin[1], bc_in, strlen(bc_in)) < 0) {
    perror("write");
    return 1;
  }
  close(bc_stdin[1]);

  // XXX waitpid?

  char bc_buf[256];
  memset(buf, 0, sizeof(bc_buf));
  int bc_n = read(bc_stdout[0], bc_buf, sizeof(bc_buf));
  if (bc_n < 0) {
    perror("read");
    return 1;
  }
  eol = strchr(bc_buf, '\n');
  if (!eol) return 1;
  eol[1] = '\0';

  //printf("got %d\n", bc_n);
  //printf("bc result: %s\n", bc_buf);

  // Write bc result to original child.
  if (write(pipe_stdin[1], bc_buf, strlen(bc_buf)) < 0) {
    perror("write");
    return 1;
  }

  //memset(buf, 0, sizeof(buf));
  n = read(pipe_stdout[0], buf, sizeof(buf));
  if (n < 0) {
    perror("read");
    return 1;
  }
  n = read(pipe_stdout[0], buf + n, sizeof(buf) - n);
  if (n < 0) {
    perror("read");
    return 1;
  }
  //char* eol = strchr(buf, '\n');
  //if (!eol)
    //return 1;
  //*eol = '\0';
  printf("got: %s\n", buf);

  printf("sent to bc: %s\n", bc_in_buf);
  printf("bc result: %s\n", bc_buf);
}
