/*
prototype that does something like `cc -E`; to be used in rc

cc -c pptest.cc -I$($HOME/src/llvm-build/bin/llvm-config --src-root)/tools/clang/include -I$($HOME/src/llvm-build/bin/llvm-config --obj-root)/tools/clang/include $($HOME/src/llvm-build/bin/llvm-config --cxxflags)
c++ -o pptest pptest.o $($HOME/src/llvm-build/bin/llvm-config --ldflags) $($HOME/src/llvm-build/bin/llvm-config --libs) -lclangParse -lclangSerialization -lclangDriver -lclangSema -lclangAnalysis -lclangEdit -lclangAST -lclangFrontend -lclangLex -lclangBasic -lz -lcurses
 */

#include <string>

#include "llvm/Config/config.h"

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/FileManager.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"

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

int main() {
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
  llvm::IntrusiveRefCntPtr<clang::HeaderSearchOptions> hso =
      new clang::HeaderSearchOptions();
  clang::HeaderSearch headers(hso, sm, diags, opts, target);
  llvm::IntrusiveRefCntPtr<clang::PreprocessorOptions> ppopts =
      new clang::PreprocessorOptions();
  VoidModuleLoader nomodules;
  clang::Preprocessor pp(ppopts, diags, opts, sm, headers, nomodules);

  delete target;
}
