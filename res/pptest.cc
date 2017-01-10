/*
prototype that does something like `cc -E`; to be used in rc

POSIX:

// FIXME: it should would be nice if there was a clang-config or a way to tell
// llvm-config that I want clang headers.
export LLVMBUILD=$HOME/src/llvm-build
cc -c pptest.cc -I$($LLVMBUILD/bin/llvm-config --src-root)/tools/clang/include -I$($LLVMBUILD/bin/llvm-config --obj-root)/tools/clang/include $($LLVMBUILD/bin/llvm-config --cxxflags)

// FIXME: also, clang-config --libs
// (and why did we take that silly curses dep :-/)
c++ -o pptest pptest.o $($LLVMBUILD/bin/llvm-config --ldflags) -lclangFrontend -lclangDriver -lclangParse -lclangSema -lclangSerialization -lclangAnalysis -lclangAST -lclangEdit -lclangLex -lclangBasic $($LLVMBUILD/bin/llvm-config --libs) $($LLVMBUILD/bin/llvm-config --system-libs)

./pptest test/accelerators.rc

Windows:

set LLVMBUILD=c:\src\llvm-build
for /F "usebackq delims=" %l in (`%LLVMBUILD%\bin\llvm-config --cxxflags`) do for /F "usebackq delims=" %s in (`%LLVMBUILD%\bin\llvm-config --src-root`) do for /F "usebackq delims=" %o in (`%LLVMBUILD%\bin\llvm-config --obj-root`) do cl /c pptest.cc /I%s\tools\clang\include /I%o\tools\clang\include %l
for /F "usebackq delims=" %l in (`%LLVMBUILD%\bin\llvm-config --ldflags`) do for /F "usebackq delims=" %b in (`%LLVMBUILD%\bin\llvm-config --libs`) do for /F "usebackq delims=" %s in (`%LLVMBUILD%\bin\llvm-config --system-libs`) do link pptest.obj %l clangFrontend.lib clangDriver.lib clangParse.lib clangSema.lib clangSerialization.lib clangAnalysis.lib clangAST.lib clangEdit.lib clangLex.lib clangBasic.lib %b %s version.lib

pptest test\accelerators.rc
 */

#include <memory>
#include <string>

#include "llvm/Config/config.h"

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/FileManager.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"

//#include "clang/Driver/Types.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Path.h"

#if defined(_WIN32)
//#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Process.h"
#endif

namespace {

// rc.exe ignores all non-preprocessor directives from .h and .c files. This
// tracks if we're in such a file.
struct MyPPCallbacks : public clang::PPCallbacks {
  MyPPCallbacks(clang::SourceManager& SM) : SM(SM) {}

  void FileChanged(clang::SourceLocation Loc,
                   FileChangeReason Reason,
                   clang::SrcMgr::CharacteristicKind FileType,
                   clang::FileID PrevFID) override {
    if (Reason != PPCallbacks::EnterFile && Reason != PPCallbacks::ExitFile)
      return;

    // rc doesn't support #line directives, so no need to worry about presumed
    // locations.
    const clang::FileEntry* FE =
        SM.getFileEntryForID(SM.getFileID(SM.getExpansionLoc(Loc)));
    if (!FE)
      return;

    StringRef Filename = FE->getName();

    // FIXME: docs only say .c and .h; but check what rc does for .hh, .hpp,
    // .cc, .cxx etc
    is_in_h_or_c =
        llvm::StringSwitch<bool>(llvm::sys::path::extension(Filename))
            .Cases(".h", ".H", ".c", ".C", true)
            .Default(false);
    // FIXME: consider using libDriver:
    //clang::driver::types::ID Ty = clang::driver::types::TY_INVALID;
    //if (const char* Ext = strrchr(Filename, '.'))
      //Ty = clang::driver::types::LookupTypeForExtension(Ext + 1);
    //is_in_h_or_c = Ty == clang::driver::types::TY_C ||
                   //Ty == clang::driver::types::TY_CHeader;

    // Dump state changes for debugging:
    //if (Reason == PPCallbacks::EnterFile)
      //printf("file entered %s\n", Filename.str().c_str());
    //else
      //printf("file exited %s\n", Filename.str().c_str());
  }

  clang::SourceManager& SM;
  bool is_in_h_or_c = false;
};

}  // namespace

int main(int argc, char* argv[]) {
  // I couldn't figure out how to make llvm::cl accept "-Ifoo" without space or
  // = between I and foo, so do manual parsing.
  std::vector<std::string> includes;
  std::vector<std::string> defines;
  while (argc > 1 && argv[1][0] == '-') {
    if (strncmp(argv[1], "-I", 2) == 0) {
      includes.push_back(argv[1] + 2);
    } else if (strncmp(argv[1], "-D", 2) == 0) {
      defines.push_back(argv[1] + 2);
    } else if (strcmp(argv[1], "--") == 0) {
      --argc;
      ++argv;
      break;
    } else {
      fprintf(stderr, "ppttest: unrecognized option `%s'\n", argv[1]);
      return 1;
    }
    --argc;
    ++argv;
  }

  // Do the long and painful dance to set up a clang::Preprocessor.
  llvm::Triple MSVCTriple("i686-pc-win32");
  clang::CompilerInstance ci;
  ci.createDiagnostics();

  std::shared_ptr<clang::TargetOptions> targetOptions =
      std::make_shared<clang::TargetOptions>();
  targetOptions->Triple = MSVCTriple.str();
  std::unique_ptr<clang::TargetInfo> target(
      clang::TargetInfo::CreateTargetInfo(ci.getDiagnostics(), targetOptions));
  ci.setTarget(target.release());

  ci.createFileManager();
  ci.createSourceManager(ci.getFileManager());

  // Add path to windows sdk headers.
  // On Windows, assume that %INCLUDE% is set (and $HOME doesn't exist there).
#if !defined(_WIN32)
  for (const char* p : {"win_sdk/Include/10.0.14393.0/um",
                        "win_sdk/Include/10.0.14393.0/shared",
                        "win_sdk/Include/10.0.14393.0/winrt",
                        "win_sdk/Include/10.0.14393.0/ucrt", "VC/include",
                        "VC/atlmfc/include"}) {
    std::string Base = std::string(getenv("HOME")) +
                       "/src/depot_tools/win_toolchain/vs_files/"
                       "d3cb0e37bdd120ad0ac4650b674b09e81be45616/";
    std::string Val = Base + p;
    ci.getHeaderSearchOpts().AddPath(Val, clang::frontend::System, false,
                                     false);
  }
#else
  // FIXME: MSVCToolChain in libDriver does this as well. But we probably don't
  // want to go through the driver?
  // Honor %INCLUDE%. It should know essential search paths with vcvarsall.bat.
  if (llvm::Optional<std::string> cl_include_dir =
          llvm::sys::Process::GetEnv("INCLUDE")) {
    llvm::SmallVector<StringRef, 8> Dirs;
    StringRef(*cl_include_dir)
        .split(Dirs, ";", /*MaxSplit=*/-1, /*KeepEmpty=*/false);
    for (StringRef Dir : Dirs)
      ci.getHeaderSearchOpts().AddPath(Dir, clang::frontend::System, false,
                                       false);
  }
#endif
  for (const std::string include : includes) {
    ci.getHeaderSearchOpts().AddPath(include, clang::frontend::Angled,
                                     /*IsFramework=*/false,
                                     /*IgnoreSysRoot=*/false);
  }

  // Define RC_INVOKED and defines from -D flags
  // (must happen before createPreprocessor).
  ci.getPreprocessorOpts().addMacroDef("RC_INVOKED");
  for (const std::string define : defines)
    ci.getPreprocessorOpts().addMacroDef(define);
  // FIXME: We end up with all kinds of default-on defines from Lex and
  // Frontend that we don't really want. Guess they don't hurt much either.


  ci.createPreprocessor(clang::TU_Complete);

  clang::LangOptions& opts = ci.getLangOpts();
  clang::FileManager& fm = ci.getFileManager();
  clang::SourceManager& sm = ci.getSourceManager();
  clang::DiagnosticConsumer* diagClient = &ci.getDiagnosticClient();
  clang::DiagnosticsEngine& diags = ci.getDiagnostics();
  clang::Preprocessor& pp = ci.getPreprocessor();

  opts.MSCompatibilityVersion = 19 * 100000;
  opts.MicrosoftExt = true;
  opts.MSVCCompat = true;

  // Add callback to be notified on file changes.
  std::unique_ptr<MyPPCallbacks> cb(new MyPPCallbacks(sm));
  MyPPCallbacks* my_callbacks = cb.get();
  pp.addPPCallbacks(std::move(cb));  // Callbacks now owned by preprocessor.

  // Load intput file into preprocessor.
  const clang::FileEntry* File = fm.getFile(argv[1]);
  if (!File) {
    llvm::errs() << "Failed to open \'" << argv[1] << "\'";
    return 1;
  }
  sm.setMainFileID(
      sm.createFileID(File, clang::SourceLocation(), clang::SrcMgr::C_User));
  pp.EnterMainSourceFile();
  diagClient->BeginSourceFile(opts, &pp);

  // Parse it
  clang::Token Tok;
  char Buffer[256];
  bool emitted_tokens_on_this_line = false;
  while (1) {
    pp.Lex(Tok);
    if (Tok.is(clang::tok::eof) || diags.hasErrorOccurred())
      break;
    if (my_callbacks->is_in_h_or_c)
      continue;

    if (Tok.isAtStartOfLine() && emitted_tokens_on_this_line) {
      llvm::outs() << '\n';
      emitted_tokens_on_this_line = false;
    }
    if (Tok.hasLeadingSpace()) {
      llvm::outs() << ' ';
    }

    if (clang::IdentifierInfo* II = Tok.getIdentifierInfo()) {
      llvm::outs() << II->getName();
    } else if (Tok.isLiteral() && !Tok.needsCleaning() &&
               Tok.getLiteralData()) {
      llvm::outs().write(Tok.getLiteralData(), Tok.getLength());
    } else if (Tok.getLength() < 256) {
      const char *TokPtr = Buffer;
      unsigned Len = pp.getSpelling(Tok, TokPtr);
      llvm::outs().write(TokPtr, Len);
    } else {
      std::string S = pp.getSpelling(Tok);
      llvm::outs().write(&S[0], S.size());
    }
    emitted_tokens_on_this_line = true;
  }
  llvm::outs() << "\n";

  diagClient->EndSourceFile();
}
