// clang -O2 dispatch_one_large.c
// Example from "Building Efficient OS X Apps", session 704 WWDC 2013

#include <stdio.h>
#include <stdlib.h>
#include <dispatch/dispatch.h>
#include <pthread.h>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <abspath>\n", argv[0]);
    return 1;
  }

  const char* path = argv[1];

  dispatch_queue_t queue =
      dispatch_queue_create("com.example.FileProcessing", NULL);

  // XXX consider `fcntl(fd, F_NOCACHE, 1);` and dispatch_io_create() with fd,
  // for read-once files.

  dispatch_io_t io = dispatch_io_create_with_path(
      DISPATCH_IO_RANDOM, path, O_RDONLY, 0, queue, ^(int error) {
        dispatch_release(queue);
        exit(error);
      });
  if (!io) {
    fprintf(stderr,
            "dispatch_io_create_with_path() failed -- pass absoute path\n");
    return 1;
  }

  dispatch_io_set_high_water(io, 32 * 1024);

  dispatch_io_read(io, 0, SIZE_MAX, queue,
                   ^(bool done, dispatch_data_t data, int error) {
                     if (error == 0)
                       dispatch_data_apply(
                           data, ^bool(dispatch_data_t region, size_t offset,
                                       const void* ptr, size_t len) {
                             /* process len bytes at ptr */
                             uint64_t tid;
                             pthread_threadid_np(NULL, &tid);
                             printf("tid %llu offset %zu len %zu\n", tid,
                                    offset, len);
                             return true;
                           });
                     if (done)
                       dispatch_io_close(io, DISPATCH_IO_STOP);
                   });

  dispatch_main();
}
