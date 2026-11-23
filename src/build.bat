@echo off
clang++ --target=i686-w64-mingw32 -std=c++23 -fno-exceptions -O2 -Wall -Wextra -Werror -shared ^
  -I. ^
  -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic ^
  -Wl,--enable-stdcall-fixup ^
  core\log.cpp ^
  core\config.cpp ^
  core\database.cpp ^
  core\caption_state.cpp ^
  core\text_layout.cpp ^
  render\gl_font.cpp ^
  render\hud_fbo_scaler.cpp ^
  render\flashlight_gl.cpp ^
  render\overlay_render.cpp ^
  goldsrc\hook_x86.cpp ^
  goldsrc\hev_restore.cpp ^
  goldsrc\sound_handler.cpp ^
  goldsrc\engine_hooks.cpp ^
  proxy\main.cpp ^
  proxy\version_proxy.cpp ^
  proxy\version.def ^
  -lgdi32 -ladvapi32 ^
  -o ..\version.dll
if %errorlevel% neq 0 exit /b %errorlevel%
echo Built ..\version.dll successfully.
