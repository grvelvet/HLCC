@echo off
clang++ --target=i686-w64-mingw32 -std=c++23 -fno-exceptions -O2 -shared ^
  -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic ^
  -Wl,--enable-stdcall-fixup ^
  main.cpp version_proxy.cpp version.def ^
  -lgdi32 -ladvapi32 ^
  -o ..\version.dll
if %errorlevel% neq 0 exit /b %errorlevel%
echo Built ..\version.dll successfully.
