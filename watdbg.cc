#include <stdio.h>

#include <map>
#include <string>

#include <windows.h>

const char* DebugEventCodeStr(DWORD code) {
#define S(x) case x: return #x
  switch (code) {
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

const char* ExceptionCodeStr(DWORD code) {
#define S(x) case x: return #x
  switch (code) {
    S(EXCEPTION_ACCESS_VIOLATION);
    S(EXCEPTION_ARRAY_BOUNDS_EXCEEDED);
    S(EXCEPTION_BREAKPOINT);
    S(EXCEPTION_DATATYPE_MISALIGNMENT);
    S(EXCEPTION_FLT_DENORMAL_OPERAND);
    S(EXCEPTION_FLT_DIVIDE_BY_ZERO);
    S(EXCEPTION_FLT_INEXACT_RESULT);
    S(EXCEPTION_FLT_INVALID_OPERATION);
    S(EXCEPTION_FLT_OVERFLOW);
    S(EXCEPTION_FLT_STACK_CHECK);
    S(EXCEPTION_FLT_UNDERFLOW);
    S(EXCEPTION_ILLEGAL_INSTRUCTION);
    S(EXCEPTION_IN_PAGE_ERROR);
    S(EXCEPTION_INT_DIVIDE_BY_ZERO);
    S(EXCEPTION_INT_OVERFLOW);
    S(EXCEPTION_INVALID_DISPOSITION);
    S(EXCEPTION_NONCONTINUABLE_EXCEPTION);
    S(EXCEPTION_PRIV_INSTRUCTION);
    S(EXCEPTION_SINGLE_STEP);
    S(EXCEPTION_STACK_OVERFLOW);
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
  Debugger(const PROCESS_INFORMATION& info) : process_info_(info) {}
  void DispatchEvent(const DEBUG_EVENT& event) {
    switch (event.dwDebugEventCode) {
      case LOAD_DLL_DEBUG_EVENT: {
        std::string dll_name = PathFromHandle(event.u.LoadDll.hFile);
        dll_names_[event.u.LoadDll.lpBaseOfDll] = dll_name;

        printf("loaded 0x%p %s\n",
               event.u.LoadDll.lpBaseOfDll, dll_name.c_str());
        break;
      }
      case UNLOAD_DLL_DEBUG_EVENT: {
        std::string dll_name = dll_names_[event.u.UnloadDll.lpBaseOfDll];
        printf("unloaded 0x%p %s\n",
               event.u.UnloadDll.lpBaseOfDll, dll_name.c_str());
        break;
      }
      case EXCEPTION_DEBUG_EVENT: {
        const EXCEPTION_RECORD& exception_record =
            event.u.Exception.ExceptionRecord;
        printf("debug event %s\n",
               ExceptionCodeStr(exception_record.ExceptionCode));
        if (exception_record.ExceptionCode == EXCEPTION_BREAKPOINT)
          HandleBreakpoint();
        break;
      }
    }
  }
  void HandleBreakpoint();

 private:
  std::map<void*, std::string> dll_names_;
  const PROCESS_INFORMATION& process_info_;
  Debugger& operator=(const Debugger&);
};

void Debugger::HandleBreakpoint() {
  CONTEXT context;
  context.ContextFlags = CONTEXT_ALL;
  GetThreadContext(process_info_.hThread, &context);

  printf("eax %08X    ebx %08X\n"
         "ecx %08X    edx %08X\n"
         "esi %08X    edi %08X\n"
         "esp %08X    ebp %08X\n"
         "eip %08X  flags %08X\n",
         context.Eax, context.Ebx, context.Ecx, context.Edx,
         context.Esi, context.Edi, context.Esp, context.Ebp,
         context.Eip, context.EFlags);
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
  Debugger dbg(process_info);
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

