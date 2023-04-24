/*
clang++ run.cc -o run -Wall -Wextra -Wconversion
./run -- ls -l
*/

#include <errno.h>
#include <getopt.h>
#include <spawn.h>
#include <stdio.h>
#include <sys/wait.h>

static void print_usage(FILE* stream, const char* program_name) {
  fprintf(stream, "usage: %s [ options ] [ -- ] command\n", program_name);
  fprintf(stream,
          "\n"
          "options:\n"
          "  -h  --help   print this message\n"
          "  -t  --turbo  run with turbo button (no-op)\n");
}

char **environ;

int main(int argc, char* argv[]) {
  // Parse options.
  const char* program_name = argv[0];
  struct option getopt_options[] = {
      {"turbo", no_argument, NULL, 't'},
      {"help", no_argument, NULL, 'h'},
      {0, 0, 0, 0},
  };
  int opt;
  while ((opt = getopt_long(argc, argv, "ht", getopt_options, NULL)) != -1) {
    switch (opt) {
      case 'h':
        print_usage(stdout, program_name);
        return 0;
      case 't':
        break;
      case '?':
        print_usage(stderr, program_name);
        return 1;
    }
  }
  argv += optind;
  argc -= optind;

  if (argc == 0) {
    print_usage(stderr, program_name);
    return 1;
  }

  // Run command.
  pid_t child_pid;

  int err = posix_spawnp(&child_pid, *argv, nullptr, nullptr, argv, environ);
  if (err != 0) {
    errno = err;
    perror("posix_spawnp");
    return 1;
  }

  int status;
  err = waitpid(child_pid, &status, 0);
  if (err < 0) {
    perror("waitpid");
    return 1;
  }
  if (err != child_pid) {
    fprintf(stderr, "unexpected waitpid() result\n");
    return 1;
  }

  if (WIFEXITED(status)) {
    printf("exit status %d\n", WEXITSTATUS(status));
  } else if (WIFSIGNALED(status)) {
    printf("interrupted by signal %d\n", WTERMSIG(status));
  } else if (WIFSTOPPED(status)) {
    // Should only happen with WUNTRACED, which we don't pass to waitpid.
    fprintf(stderr, "unexpected WIFSTOPPED\n");
    return 1;
  }
}
