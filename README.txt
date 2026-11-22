HLCC Release (Half-Life + Opposing Force + Blue Shift)
=======================================================
1. Скопируйте все файлы и папки из этой директории в корневую папку Half-Life
   (где находится hl.exe) с заменой при необходимости.
2. Запускайте игру в режиме OpenGL (Half-Life / Opposing Force / Blue Shift).
   Для Linux / Steam Deck (Steam Proton): включите Proton в свойствах игры
   и добавьте в параметры запуска:
   WINEDLLOVERRIDES="version=n,b" %command%
3. Настройки стиля (default / hl2), шрифта, ролей, HUD и OpenGL-фонарика находятся в subtitles.ini.

Сборка из исходников
--------------------
- Тулчейн: LLVM-MinGW (UCRT), 32-битный таргет i686-w64-mingw32 (для x86 hl.exe).
  Установка через winget: winget install MartinStorsjo.LLVM-MinGW.UCRT
  (clang++ должен быть доступен в PATH).
- Сборка version.dll: запустите src\build.bat из папки src.
- Упаковка релиза: powershell -ExecutionPolicy Bypass -File package_release.ps1

Совместимость со сторонними русификаторами и модами
--------------------------------------------------
- Русские озвучки: поддерживаются при стандартных именах звуков; при желании тайминги можно поправить в subtitles_ru.tsv.
- Текстовые русификаторы: не конфликтуют, если не перезаписывают version.dll в корне Half-Life.
- Моды с кастомным HUD / врапперами (ReShade и т.п.): при визуальных конфликтах отключите скейл/фонарик в subtitles.ini (hud_scale=1.0, hud_compact_armor=0, enable_gl_flashlight=0).
