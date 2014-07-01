#include <stdio.h>

#include <map>
#include <string>

#include <windows.h>

#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
bool kUsePdbs = true;

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
      case CREATE_PROCESS_DEBUG_EVENT: {
        const CREATE_PROCESS_DEBUG_INFO& info = event.u.CreateProcessInfo;
        std::string exe_name = PathFromHandle(info.hFile);
        LoadSymbols(  // TODO: is nDebugInfoSize the right size here?
            info.hFile, exe_name, info.lpBaseOfImage, info.nDebugInfoSize);
      } break;
      case LOAD_DLL_DEBUG_EVENT: {
        const LOAD_DLL_DEBUG_INFO& info = event.u.LoadDll;
        std::string dll_name = PathFromHandle(info.hFile);
        LoadSymbols(  // TODO: is nDebugInfoSize the right size here?
            info.hFile, dll_name, info.lpBaseOfDll, info.nDebugInfoSize);
        dll_names_[info.lpBaseOfDll] = dll_name;

        if (!kUsePdbs)
          printf("loaded 0x%p %s\n", info.lpBaseOfDll, dll_name.c_str());
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
  void LoadSymbols(
      HANDLE file, const std::string& path, void* address, DWORD size);
  void PrintBacktrace(HANDLE thread, CONTEXT* context);

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

  PrintBacktrace(process_info_.hThread, &context);
}

void Debugger::LoadSymbols(
    HANDLE file, const std::string& path, void* address, DWORD size) {
  if (!kUsePdbs) return;

  DWORD64 base = SymLoadModule64(
      process_info_.hProcess, file, path.c_str(), 0, (DWORD64)address, size);

  IMAGEHLP_MODULE64 module = { sizeof IMAGEHLP_MODULE64 };
  BOOL b = SymGetModuleInfo64(process_info_.hProcess, base, &module);
  if (b && module.SymType == SymPdb)
    printf("Loaded symbols for %s\n", path.c_str());
  else
    printf("No symbols for %s\n", path.c_str());
}

void Debugger::PrintBacktrace(HANDLE thread, CONTEXT* context) {
  // See base/debug/stack_track_win.cc in Chromium and
  // http://devonstrawntech.tumblr.com/post/15878429193/how-to-write-a-windows-debugger-references#Stack

  printf("Stack:\n");

  STACKFRAME64 stack = {0};
#if defined(_WIN64)
  int machine_type = IMAGE_FILE_MACHINE_AMD64;
  stack.AddrPC.Offset = context->Rip;
  stack.AddrFrame.Offset = context->Rbp;
  stack.AddrStack.Offset = context->Rsp;
#else
  int machine_type = IMAGE_FILE_MACHINE_I386;
  stack.AddrPC.Offset = context->Eip;
  stack.AddrFrame.Offset = context->Ebp;
  stack.AddrStack.Offset = context->Esp;
#endif
  stack.AddrPC.Mode = AddrModeFlat;
  stack.AddrFrame.Mode = AddrModeFlat;
  stack.AddrStack.Mode = AddrModeFlat;
  while (StackWalk64(machine_type, process_info_.hProcess, thread,
                     &stack, context, NULL, &SymFunctionTableAccess64,
                     &SymGetModuleBase64, NULL) &&
         stack.AddrReturn.Offset) {
    printf("0x%p\n", stack.AddrPC.Offset);
  }
}

int main() {
  char kCommand[] = "../../ninja/ninja.exe -h";

  PROCESS_INFORMATION process_info = {};
  STARTUPINFO startup_info = { sizeof(STARTUPINFO) };
  GetStartupInfo(&startup_info);

  if (!CreateProcess(NULL, kCommand, NULL, NULL, TRUE, DEBUG_ONLY_THIS_PROCESS,
                     NULL, NULL, &startup_info, &process_info)) {
    fprintf(stderr, "failed to run \"%s\"\n", kCommand);
    exit(1);
  }

  if (kUsePdbs) SymInitialize(process_info.hProcess, NULL, FALSE);

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

  if (kUsePdbs) SymCleanup(process_info.hProcess);
}

