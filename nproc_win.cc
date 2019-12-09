// cl nproc_win
// Be sure to build as 64-bit; in 32-bit some of the functions cap their results

#include <stdio.h>
#include <windows.h>

#include <vector>

int main() {
  // Original (capped at 32):
  {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    printf("GetSystemInfo(): %d\n", info.dwNumberOfProcessors);
  }
  // https://github.com/ninja-build/ninja/commit/8c8834acffdc0da0d94119725929acc712c9dfad
  {
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);
    printf("GetNativeSystemInfo(): %d\n", info.dwNumberOfProcessors);
  }
  // https://github.com/ninja-build/ninja/commit/a3a5d60622eb7330b8d82ff6620d28e3b90c6848
  // Note: Returns at most 32 for 32-bit executables, more for 64-bit binaries.
  printf("GetActiveProcessorCount(): %d\n",
         GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
  // https://github.com/ninja-build/ninja/pull/1674
  {
    // Need to use GetLogicalProcessorInformationEx to get real core count on
    // machines with >64 cores. See https://stackoverflow.com/a/31209344/21475
    DWORD len = 0;
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len)
          && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
      std::vector<char> buf(len);
      int cores = 0;
      if (GetLogicalProcessorInformationEx(RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
              buf.data()), &len)) {
        for (DWORD i = 0; i < len; ) {
          PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info =
              reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                buf.data() + i);
          if (info->Relationship == RelationProcessorCore &&
              info->Processor.GroupCount == 1) {
            for (KAFFINITY core_mask = info->Processor.GroupMask[0].Mask;
                 core_mask; core_mask >>= 1) {
              cores += (core_mask & 1);
            }
          }
          i += info->Size;
        }
        if (cores) {
          printf("GetLogicalProcessorInformationEx(): %d\n", cores);
        }
      }
    }
  }
}
