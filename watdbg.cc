#include <stdio.h>

#include <map>
#include <string>

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

std::string PathFromHandle(HANDLE h) {
  static_assert(sizeof TCHAR == 1, "ascii only");
  char buf[MAX_PATH];
  if (!GetFinalPathNameByHandle(h, buf, sizeof(buf) / sizeof(buf[0]), 0)) {
    fprintf(stderr, "failed to get path for handle\n");
    exit(1);
  }
  return buf;
}

class Debugger {
 public:
  void DispatchEvent(const DEBUG_EVENT& event) {
    switch (event.dwDebugEventCode) {
      case LOAD_DLL_DEBUG_EVENT: {
        std::string dll_name = PathFromHandle(event.u.LoadDll.hFile);
        m_dllNames[event.u.LoadDll.lpBaseOfDll] = dll_name;

        printf("loaded 0x%p %s\n",
               event.u.LoadDll.lpBaseOfDll, dll_name.c_str());
        break;
      }
      case UNLOAD_DLL_DEBUG_EVENT: {
        std::string dll_name = m_dllNames[event.u.UnloadDll.lpBaseOfDll];
        printf("unloaded 0x%p %s\n",
               event.u.UnloadDll.lpBaseOfDll, dll_name.c_str());
        break;
      }
    }
  }

 private:
  std::map<void*, std::string> m_dllNames;
};

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
  Debugger dbg;
  DEBUG_EVENT debug_event = {};
  while (WaitForDebugEvent(&debug_event, INFINITE)) {
    printf("got debug event %s\n",
           DebugEventCodeStr(debug_event.dwDebugEventCode));
    dbg.DispatchEvent(debug_event);
    if (debug_event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT)
      break;

    ContinueDebugEvent(debug_event.dwProcessId, debug_event.dwThreadId,
                       DBG_EXCEPTION_NOT_HANDLED);
  }
}

