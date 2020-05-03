// Based on:
// https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlea

// clang-cl getfilename.cc -DUNICODE -D_UNICODE

#include <windows.h>
#include <tchar.h>
#include <stdio.h>

#define BUFSIZE MAX_PATH

void usage(TCHAR* self) {
  _tprintf(TEXT("usage: %s [--volume=dos|guid|none|opened] [--file=normalized|opened] <file_name>"), self);
}

int __cdecl _tmain(int argc, TCHAR *argv[]) {
  TCHAR *File = nullptr;
  DWORD Flags = 0;

  for (int i = 1; i < argc; ++i) {
    if (_tcscmp(argv[i], TEXT("-h")) == 0 ||
        _tcscmp(argv[i], TEXT("--help")) == 0)
      usage(argv[0]);
    else if (_tcscmp(argv[i], TEXT("--volume=dos")) == 0)
      Flags |= VOLUME_NAME_DOS;
    else if (_tcscmp(argv[i], TEXT("--volume=guid")) == 0)
      Flags |= VOLUME_NAME_GUID;
    else if (_tcscmp(argv[i], TEXT("--volume=nt")) == 0)
      Flags |= VOLUME_NAME_NT;
    else if (_tcscmp(argv[i], TEXT("--volume=none")) == 0)
      Flags |= VOLUME_NAME_NONE;
    else if (_tcscmp(argv[i], TEXT("--file=normalized")) == 0)
      Flags |= FILE_NAME_OPENED;
    else if (_tcscmp(argv[i], TEXT("--file=opened")) == 0)
      Flags |= FILE_NAME_OPENED;
    else
      File = argv[i];
  }
  if (!File) {
    usage(argv[0]);
    return 1;
  }

  HANDLE hFile = CreateFile(File,                  // file to open
                            GENERIC_READ,          // open for reading
                            FILE_SHARE_READ,       // share for reading
                            NULL,                  // default security
                            OPEN_EXISTING,         // existing file only
                            FILE_ATTRIBUTE_NORMAL, // normal file
                            NULL);                 // no attr. template

  if (hFile == INVALID_HANDLE_VALUE) {
    _tprintf(TEXT("Could not open file '%s' (error %lu)"), File, GetLastError());
    return 1;
  }

  TCHAR Path[BUFSIZE];
  DWORD dwRet = GetFinalPathNameByHandle(hFile, Path, BUFSIZE, Flags);

  if (dwRet < BUFSIZE)
    _tprintf(TEXT("%s"), Path);
  else
    printf("The required buffer size is %lu.\n", dwRet);

  CloseHandle(hFile);
}
