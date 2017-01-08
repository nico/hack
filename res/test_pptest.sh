#/bin/bash
./pptest ~/src/chrome/src/chrome/app/chrome_exe.rc 
./pptest ~/src/chrome/src/chrome/app/chrome_dll.rc 
./pptest ~/src/chrome/src/ui/gfx/icon_util_unittests.rc
# XXX needs /Isrc/chrome/src:
# ./pptest ~/src/chrome/src/content/shell/app/shell.rc
# ./pptest ~/src/chrome/src/chrome/test/data/resource.rc
# XXX more interesting with /DGOOGLE_CHROME_BUILD
./pptest ~/src/chrome/src/chrome/installer/setup/setup.rc
./pptest ~/src/chrome/src/chrome/installer/gcapi/gcapi_test.rc
./pptest ~/src/chrome/src/third_party/swiftshader/src/D3D9/D3D9.rc
./pptest ~/src/llvm-rw/resources/windows_version_resource.rc
