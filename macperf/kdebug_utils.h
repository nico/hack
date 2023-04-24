#pragma once

#include "kdebug.h"
#include "types.h"

// From https://gist.github.com/ibireme/173517c208c7dc333ba962c1f0d67d12

/// Clean up trace buffers and reset ktrace/kdebug/kperf.
/// @return 0 on success.
static int kdebug_reset(void) {
  int mib[3] = {CTL_KERN, KERN_KDEBUG, KERN_KDREMOVE};
  return sysctl(mib, 3, NULL, NULL, NULL, 0);
}

/// Disable and reinitialize the trace buffers.
/// @return 0 on success.
static int kdebug_reinit(void) {
  int mib[3] = {CTL_KERN, KERN_KDEBUG, KERN_KDSETUP};
  return sysctl(mib, 3, NULL, NULL, NULL, 0);
}

/// Set debug filter.
static int kdebug_setreg(kd_regtype* kdr) {
  int mib[3] = {CTL_KERN, KERN_KDEBUG, KERN_KDSETREG};
  usize size = sizeof(kd_regtype);
  return sysctl(mib, 3, kdr, &size, NULL, 0);
}

/// Set maximum number of trace entries (kd_buf).
/// Only allow allocation up to half the available memory (sane_size).
/// @return 0 on success.
static int kdebug_trace_setbuf(int nbufs) {
  int mib[4] = {CTL_KERN, KERN_KDEBUG, KERN_KDSETBUF, nbufs};
  return sysctl(mib, 4, NULL, NULL, NULL, 0);
}

/// Enable or disable kdebug trace.
/// Trace buffer must already be initialized.
/// @return 0 on success.
static int kdebug_trace_enable(bool enable) {
  int mib[4] = {CTL_KERN, KERN_KDEBUG, KERN_KDENABLE, enable};
  return sysctl(mib, 4, NULL, 0, NULL, 0);
}

/// Retrieve trace buffer information from kernel.
/// @return 0 on success.
static int kdebug_get_bufinfo(kbufinfo_t* info) {
  if (!info)
    return -1;
  int mib[3] = {CTL_KERN, KERN_KDEBUG, KERN_KDGETBUF};
  size_t needed = sizeof(kbufinfo_t);
  return sysctl(mib, 3, info, &needed, NULL, 0);
}

/// Retrieve trace buffers from kernel.
/// @param buf Memory to receive buffer data, array of `kd_buf`.
/// @param len Length of `buf` in bytes.
/// @param count Number of trace entries (kd_buf) obtained.
/// @return 0 on success.
static int kdebug_trace_read(void* buf, usize len, usize* count) {
  if (count)
    *count = 0;
  if (!buf || !len)
    return -1;

  // Note: the input and output units are not the same.
  // input: bytes
  // output: number of kd_buf
  int mib[3] = {CTL_KERN, KERN_KDEBUG, KERN_KDREADTR};
  int ret = sysctl(mib, 3, buf, &len, NULL, 0);
  if (ret != 0)
    return ret;
  *count = len;
  return 0;
}

/// Block until there are new buffers filled or `timeout_ms` have passed.
/// @param timeout_ms timeout milliseconds, 0 means wait forever.
/// @param suc set true if new buffers filled.
/// @return 0 on success.
static int kdebug_wait(usize timeout_ms, bool* suc) {
  if (timeout_ms == 0)
    return -1;
  int mib[3] = {CTL_KERN, KERN_KDEBUG, KERN_KDBUFWAIT};
  usize val = timeout_ms;
  int ret = sysctl(mib, 3, NULL, &val, NULL, 0);
  if (suc)
    *suc = !!val;
  return ret;
}
