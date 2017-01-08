/*
prototype that does something like `cc -E`; to be used in rc

// FIXME: it should would be nice if there was a clang-config or a way to tell
// llvm-config that I want clang headers.
cc -c pptest.cc -I$($HOME/src/llvm-build/bin/llvm-config --src-root)/tools/clang/include -I$($HOME/src/llvm-build/bin/llvm-config --obj-root)/tools/clang/include $($HOME/src/llvm-build/bin/llvm-config --cxxflags)

// FIXME: why doesn't llvm-config --libs include -lz -lcurses?
// FIXME: also, clang-config --libs
// (and why did we take that silly curses dep :-/)
c++ -o pptest pptest.o $($HOME/src/llvm-build/bin/llvm-config --ldflags) $($HOME/src/llvm-build/bin/llvm-config --libs) -lclangParse -lclangSerialization -lclangDriver -lclangSema -lclangAnalysis -lclangEdit -lclangAST -lclangFrontend -lclangLex -lclangBasic -lz -lcurses

./pptest test/accelerators.rc
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

//#include "clang/Driver/Types.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Path.h"

namespace {
class VoidModuleLoader : public clang::ModuleLoader {
  clang::ModuleLoadResult loadModule(
      clang::SourceLocation ImportLoc,
      clang::ModuleIdPath Path,
      clang::Module::NameVisibilityKind Visibility,
      bool IsInclusionDirective) override {
    return clang::ModuleLoadResult();
  }

  void makeModuleVisible(clang::Module* Mod,
                         clang::Module::NameVisibilityKind Visibility,
                         clang::SourceLocation ImportLoc) override {}

  clang::GlobalModuleIndex* loadGlobalModuleIndex(
      clang::SourceLocation TriggerLoc) override {
    return nullptr;
  }
  bool lookupMissingImports(StringRef Name,
                            clang::SourceLocation TriggerLoc) override {
    return 0;
  }
};

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
  // Do the long and painful dance to set up a clang::Preprocessor.
  clang::DiagnosticOptions* diagnosticOptions = new clang::DiagnosticOptions;
  clang::DiagnosticConsumer* diagClient =
      new clang::TextDiagnosticPrinter(llvm::outs(), diagnosticOptions);
  llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> diagIDs =
      new clang::DiagnosticIDs;
  // Takes ownership of diagnosticOptions and diagClient:
  clang::DiagnosticsEngine diags(diagIDs, diagnosticOptions, diagClient);
  clang::LangOptions opts;
  std::shared_ptr<clang::TargetOptions> targetOptions =
      std::make_shared<clang::TargetOptions>();
  targetOptions->Triple = llvm::sys::getDefaultTargetTriple();
  clang::TargetInfo* target =
      clang::TargetInfo::CreateTargetInfo(diags, targetOptions);
  clang::FileSystemOptions fileSystemOptions;
  clang::FileManager fm(fileSystemOptions);
  clang::SourceManager sm(diags, fm);
  std::shared_ptr<clang::HeaderSearchOptions> hso =
      std::make_shared<clang::HeaderSearchOptions>();
  clang::HeaderSearch headers(hso, sm, diags, opts, target);
  std::shared_ptr<clang::PreprocessorOptions> ppopts =
      std::make_shared<clang::PreprocessorOptions>();
  VoidModuleLoader nomodules;
  clang::Preprocessor pp(ppopts, diags, opts, sm, headers, nomodules);

  // XXX define RC_INVOKED

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
  do {
    pp.Lex(Tok);
    if (diags.hasErrorOccurred())
      break;
    if (my_callbacks->is_in_h_or_c)
      continue;
    pp.DumpToken(Tok);
    llvm::errs() << "\n";
  } while (Tok.isNot(clang::tok::eof));

  diagClient->EndSourceFile();

  delete target;
}
