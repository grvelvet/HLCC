# HLCC — Half-Life Closed Captions (C++23)

**Русский** | [English](README.en.md)

Мод-плагин для Steam-трилогии **Half-Life**, **Opposing Force** и **Blue Shift** без изменения файлов игры:

- **Субтитры в стиле HL2:** речь NPC, радио, VOX, костюм (HEV/PCV) и важные игровые SFX с защитой от спама.
- **Учёт дистанции и преград:** фильтрация звуков за глухими стенами, корректная работа через стекло и в скриптовых сценах.
- **Два стиля рендера (OpenGL):** динамическая плашка (`default`) или фиксированное окно с плавным скроллом (`hl2`).
- **Автолокализация (EN/RU):** переключение языка по настройкам Steam, перевод заголовков глав и автотайминги по `.wav` для сторонних озвучек.
- **Улучшения графики и HUD:** масштабирование старого HUD и фикс ванильного HUD в дополнениях (`bshift`, `gearbox`), плюс 3D OpenGL-фонарик.

## Установка (из GitHub Releases)
1. Скачайте архив `HLCC-win32.zip` со страницы **Releases**.
2. Распакуйте содержимое в корневую папку **Half-Life** (рядом с `hl.exe`) с заменой файлов.
3. Запускайте игру в режиме **OpenGL**.
4. **Для Linux / Steam Deck (Steam Proton):** в свойствах игры включите **Proton** (нативный Linux-билд `hl_linux` не загружает `version.dll`) и добавьте в параметры запуска:
   ```text
   WINEDLLOVERRIDES="version=n,b" %command%
   ```
5. При необходимости настройте шрифт, пресет, цвета, профили `[Mod:*]` и фильтры в `subtitles.ini`.

## Документация
- [MODDER_GUIDE.md](MODDER_GUIDE.md) — архитектура `version.dll`, пайплайн звука/HUD и адаптация под сторонние GoldSrc-моды (EN)
- [TRANSLATION_GUIDE.md](TRANSLATION_GUIDE.md) — руководство по переводу субтитров и заголовков на другие языки (EN)

## Структура репозитория
- `src/` — исходный код `version.dll` (C++23) и скрипт сборки `build.bat`
- `valve/` — базы субтитров (`subtitles_ru.tsv`, `subtitles_en.tsv`) и заголовков (`titles_ru.tsv`) для Half-Life
- `gearbox/` — базы субтитров и заголовков для Opposing Force
- `bshift/` — базы субтитров и заголовков для Blue Shift
- `subtitles.ini` — конфигурация по умолчанию (включая профили `[Mod:bshift]`, `[Mod:gearbox]`, `[Mod:hl1mmod]`)
- `package_release.ps1` — скрипт сборки готового архива `release/HLCC-win32.zip`

## Сборка из исходников
Требуется **LLVM-MinGW (UCRT)** (`winget install MartinStorsjo.LLVM-MinGW.UCRT`) с таргетом `i686-w64-mingw32`:

```bat
cd src
build.bat
```

Собранный `version.dll` появится в корне репозитория. Для упаковки релизного архива запустите:

```powershell
powershell -ExecutionPolicy Bypass -File package_release.ps1
```

## Сборка через GitHub Actions
В `.github/workflows/build.yml` настроена автоматическая сборка на `windows-latest` с тем же тулчейном **LLVM-MinGW (UCRT)**:
- При любом `push` / `pull_request` / ручном запуске (`workflow_dispatch`) проверяются TSV-базы, собирается `version.dll` через `src/build.bat` и выгружается артефакт `HLCC-win32`.
- При пуше тега вида `v*` (например, `git tag v1.0b && git push origin v1.0b`) автоматически создаётся **GitHub Release** с прикреплённым `HLCC-win32.zip`.
