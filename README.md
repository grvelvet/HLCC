# HLCC — Half-Life Closed Captions (C++23)

Мод-прокси (`version.dll`) для **Half-Life** (`valve`), **Half-Life: Opposing Force** (`gearbox`), **Half-Life: Blue Shift** (`bshift`) и сторонних GoldSrc-модов. Добавляет современные субтитры в стиле Half-Life 2 (OpenGL-рендер), перевод заголовков глав, FBO-масштабирование HUD для дополнений и объёмный 3D OpenGL-фонарик.

## Возможности
- **Субтитры речи и объявлений:** диалоги NPC (включая именованных персонажей — Розенберг, Уолтер, Гарольд, Отис), радио, VOX, HEV (включая опционально восстановленные реплики костюма в оригинальном Half-Life).
- **Контекстные звуки (SFX в стиле HL2):** важные геймплейные сигналы, угрозы, тревоги, взрывы, счётчик Гейгера, зарядники (отдельно учтены костюм HEV в `valve` и броня PCV в `gearbox`), с защитой от спама и приоритетом речи.
- **Проверка дистанции и преград:** учитывает видимость, стекло (`glass_max_distance`), настраиваемые списки исключений (`no_occlusion_prefixes`), громкость и плавное затухание при большом удалении от источника.
- **Два стиля оформления (`style=default | hl2`):** классическая динамическая плашка (`default`) или фиксированная плашка HL2 с построчным плавным скроллом (`hl2`), настраиваемые в `subtitles.ini`.
- **Перевод заголовков глав и досье:** поддержка `titles_ru.tsv` для `valve`, `gearbox` и `bshift`.
- **Масштабируемый HUD и компактная броня (`bshift` / `gearbox`):** FBO-скейлинг оригинальных спрайтов (`hud_scale=1.5`) и сближенная броня (`hud_compact_armor=1`); настраивается через профили `[Mod:<gameDir>]` в `subtitles.ini`, оригинальный Half-Life (`valve`) и сторонние моды не затрагиваются.
- **OpenGL 3D-фонарик (`enable_gl_flashlight=1`):** объёмный прожектор в стиле HL2 с инерцией луча и поддержкой MSAA (`glBlitFramebuffer`).
- **Авто-тайминги по `.wav` (`auto_wav_timing`):** опциональный расчёт длительности по реальным `.wav`-файлам (удобно для сторонних озвучек).

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
