#include <stdio.h>

#include <windows.h>

const char* DebugEventCodeStr(DWORD type) {
#define S(x) case x: return #x
  switch (type) {
    S(CREATE_PROCESS_DEBUG_EVENT);
    S(CREATE_THREAD_DEBUG_EVENT);
    S(EXCEPTION_DEBUG_EVENT);
    S(EXIT_PROCESS_DEBUG_EVENT);
    S(EXIT_THREAD_DEBUG_EVENT);
    S(LOAD_DLL_DEBUG_EVENT);
    S(OUTPUT_DEBUG_STRING_EVENT);
    S(RIP_EVENT);
    S(UNLOAD_DLL_DEBUG_EVENT);
    default: return "(unknown debug event code)";
  }
#undef S
}

int main() {
  char kCommand[] = "../ninja/ninja.exe -h";

  PROCESS_INFORMATION process_info = {};
  STARTUPINFO startup_info = { sizeof(STARTUPINFO) };
  GetStartupInfo(&startup_info);

  if (!CreateProcess(NULL, kCommand, NULL, NULL, TRUE, DEBUG_ONLY_THIS_PROCESS,
                     NULL, NULL, &startup_info, &process_info)) {
    fprintf(stderr, "failed to run \"%s\"\n", kCommand);
    exit(1);
  }

  printf("about to enter loop\n");
  DEBUG_EVENT debug_event = {};
  while (WaitForDebugEvent(&debug_event, INFINITE)) {
    // TODO do stuff
    printf("got debug event %s\n",
           DebugEventCodeStr(debug_event.dwDebugEventCode));
    if (debug_event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT)
      break;

    ContinueDebugEvent(debug_event.dwProcessId, debug_event.dwThreadId,
                       DBG_EXCEPTION_NOT_HANDLED);
  }

  //DWORD exit_code;
  //CloseHandle(process_info.hThread);

  //WaitForSingleObject(process_info.hProcess, INFINITE);
  //GetExitCodeProcess(process_info.hProcess, &exit_code);
  //CloseHandle(process_info.hProcess);
  //exit(exit_code);
}

