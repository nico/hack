// Writes many medium-size files to disk in various ways.
/*
clang -O2 -o foo write.c
 */

#include <dispatch/dispatch.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const int kNumFiles = 100;
const int kChunkSize = 4 << 20;
const int kIntChunkSize = kChunkSize / sizeof(uint32_t);

void direct_fwrite(uint32_t* data) {
  char buf[80];
  for (int fi = 0; fi < kNumFiles; ++fi) {
    sprintf(buf, "f%02d.out", fi);
    FILE* f = fopen(buf, "wb");
    fwrite(data + fi * kIntChunkSize, 1, kChunkSize, f);
    fclose(f);
  }

  free(data);
}

void dispatch_apply_fwrite(uint32_t* data) {
  dispatch_queue_t queue =
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
  dispatch_group_t group = dispatch_group_create();

  for (int fi = 0; fi < kNumFiles; ++fi)
    dispatch_group_enter(group);  // There's no dispatch_group_apply().

  dispatch_apply(kNumFiles, queue, ^(size_t fi) {
      char buf[80];
      sprintf(buf, "f%02d.out", (int)fi);
      FILE* f = fopen(buf, "wb");
      fwrite(data + fi * kIntChunkSize, 1, kChunkSize, f);
      fclose(f);
      dispatch_group_leave(group);
  });

  dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
  dispatch_release(group);
  free(data);
}

void dispatch_async_fwrite(uint32_t* data) {
  dispatch_queue_t queue =
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
  dispatch_group_t group = dispatch_group_create();

  for (int fi = 0; fi < kNumFiles; ++fi) {
    dispatch_group_async(group, queue, ^{
      char buf[80];
      sprintf(buf, "f%02d.out", (int)fi);
      FILE* f = fopen(buf, "wb");
      fwrite(data + fi * kIntChunkSize, 1, kChunkSize, f);
      fclose(f);
    });
  }

  dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
  dispatch_release(group);
  free(data);
}

void dispatch_io(uint32_t* data) {
  dispatch_queue_t queue =
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
  dispatch_group_t group = dispatch_group_create();

  char buf[80];
  for (int fi = 0; fi < kNumFiles; ++fi) {
    sprintf(buf, "f%02d.out", fi);

#if 0
    // FIXME: Why does this not work? (`f` is always 0)
    dispatch_io_t f = dispatch_io_create_with_path(
        DISPATCH_IO_STREAM, buf, O_WRONLY | O_CREAT, 0644, queue,
        ^(int error) {});
    if (f == 0)
      perror("wat");
#else
    int fd = open(buf, O_WRONLY | O_CREAT, 0644);
    dispatch_io_t f =
        dispatch_io_create(DISPATCH_IO_STREAM, fd, queue, ^(int error) {});
#endif

    dispatch_data_t ddata =
        dispatch_data_create(data + fi * kIntChunkSize, kChunkSize, queue, ^{});

    dispatch_group_enter(group);
    dispatch_io_write(f, 0, ddata, queue,
      ^(bool done, dispatch_data_t data, int error) {
          if (done)
            dispatch_group_leave(group);
    });

    dispatch_release(ddata);
    dispatch_io_close(f, 0);
    dispatch_release(f);
  }

  dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
  dispatch_release(group);
  free(data);
}

int main(int argc, char* argv[]) {

  uint32_t* data = (uint32_t*)malloc(kNumFiles * kChunkSize * sizeof(uint32_t));

  // Too slow, so don't initialize the written data:
  //for (int i = 0; i < kNumFiles * kChunkSize; ++i) data[i] = i;

  enum {
    kDirectFwrite, kDispatchApplyWrite, kDispatchAsyncWrite, kDispatchIO,
  } mode = kDirectFwrite;
  if (argc > 1) {
    if (strcmp(argv[1], "dispatch_apply_write") == 0)
      mode = kDispatchApplyWrite;
    else if (strcmp(argv[1], "dispatch_async_write") == 0)
      mode = kDispatchAsyncWrite;
    else if (strcmp(argv[1], "dispatch_io") == 0)
      mode = kDispatchIO;
  }

  switch (mode) {
    case kDirectFwrite: direct_fwrite(data); break;
    case kDispatchApplyWrite: dispatch_apply_fwrite(data); break;
    case kDispatchAsyncWrite: dispatch_async_fwrite(data); break;
    case kDispatchIO: dispatch_io(data); break;
  }
}
