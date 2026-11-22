# HLCC — Half-Life Closed Captions (C++23)

Мод-прокси (`version.dll`) для **Half-Life** (`valve`), **Half-Life: Opposing Force** (`gearbox`) и **Half-Life: Blue Shift** (`bshift`), добавляющий современные субтитры в стиле Half-Life 2 (OpenGL-рендер), переводы глав, HiDPI HUD для Opposing Force и 3D OpenGL-фонарик.

## Возможности
- **Субтитры речи и объявлений:** диалоги NPC (включая именованных персонажей — Розенберг, Уолтер, Гарольд, Отис), радио, VOX, HEV (включая опционально восстановленные реплики костюма в оригинальном Half-Life).
- **Контекстные звуки (SFX в стиле HL2):** важные геймплейные сигналы, угрозы, тревоги, взрывы, счётчик Гейгера, зарядники (отдельно учтены костюм HEV в `valve` и броня PCV в `gearbox`), с защитой от спама и приоритетом речи.
- **Проверка дистанции и преград:** учитывает видимость, стекло (`glass_max_distance`), настраиваемые списки исключений (`no_occlusion_prefixes`) и громкость.
- **Два стиля оформления (`style=default | hl2`):** классическая динамическая плашка (`default`) или фиксированная плашка HL2 с построчным плавным скроллом (`hl2`), настраиваемые в `subtitles.ini`.
- **Перевод заголовков глав и досье:** поддержка `titles_ru.tsv` для `valve`, `gearbox` и `bshift`.
- **Масштабируемый HUD и компактная броня (`bshift` / `gearbox`):** FBO-скейлинг оригинальных спрайтов (`hud_scale=1.5`) и сближенная броня (`hud_compact_armor=1`); оригинальный Half-Life (`valve`) не затрагивается.
- **OpenGL 3D-фонарик (`enable_gl_flashlight=1`):** объёмный прожектор в стиле HL2 с инерцией луча для `valve` и `bshift` (в `gearbox` автоматически отключён ради ПНВ).
- **Авто-тайминги по `.wav` (`auto_wav_timing`):** опциональный расчёт длительности по реальным `.wav`-файлам (удобно для сторонних озвучек).

## Установка (из GitHub Releases)
1. Скачайте архив `HLCC-win32.zip` со страницы **Releases**.
2. Распакуйте содержимое в корневую папку **Half-Life** (рядом с `hl.exe`) с заменой файлов.
3. Запускайте игру в режиме **OpenGL**.
4. **Для Linux / Steam Deck (Steam Proton):** в свойствах игры включите **Proton** (нативный Linux-билд `hl_linux` не загружает `version.dll`) и добавьте в параметры запуска:
   ```text
   WINEDLLOVERRIDES="version=n,b" %command%
   ```
5. При необходимости настройте шрифт, пресет, цвета и фильтры в `subtitles.ini`.

## Структура репозитория
- `src/` — исходный код `version.dll` (C++23) и скрипт сборки `build.bat`
- `valve/` — базы субтитров (`subtitles_ru.tsv`, `subtitles_en.tsv`) и заголовков (`titles_ru.tsv`) для Half-Life
- `gearbox/` — базы субтитров и заголовков для Opposing Force
- `bshift/` — базы субтитров и заголовков для Blue Shift
- `subtitles.ini` — конфигурация по умолчанию
- `package_release.ps1` — скрипт сборки готового архива `release/HLCC-win32.zip` для GitHub Releases
- `MODDER_GUIDE.md` — архитектура проекта и руководство для мододелов (EN)
- `TRANSLATION_GUIDE.md` — руководство по переводу субтитров на другие языки (EN)
- `PORTING_BS_OP4_AGENT_GUIDE.md` — руководство по портированию (включая чек-лист для Blue Shift)

## Сборка из исходников
Требуется `clang++` с таргетом `i686-w64-mingw32` (Win32 x86, MinGW-w64 / LLVM-MinGW):

```bat
cd src
build.bat
```

Собранный `version.dll` появится в корне репозитория. Для упаковки релизного архива запустите:

```powershell
.\package_release.ps1
```

## Сборка через GitHub Actions
В `.github/workflows/build.yml` настроена автоматическая сборка на `windows-latest` с тем же тулчейном **LLVM-MinGW (UCRT)**:
- При любом `push` / `pull_request` / ручном запуске (`workflow_dispatch`) проверяются TSV-базы, собирается `version.dll` через `src/build.bat` и выгружается артефакт `HLCC-win32`.
- При пуше тега вида `v*` (например, `git tag v1.0b && git push origin v1.0b`) автоматически создаётся **GitHub Release** с прикреплённым `HLCC-win32.zip`.

## Статистика проекта

### Исходный код (`src/` — 15 файлов, 4 453 строки, 189 930 байт)
| Файл | Строк | Размер | Назначение |
| :--- | ---: | ---: | :--- |
| `sound_handler.hpp` | 499 | 21 189 Б | Фильтрация звуков, окклюзия, кулдауны SFX, авто-тайминги WAV, HEV |
| `gl_font.hpp` | 497 | 20 851 Б | GDI-атлас глифов и OpenGL-отрисовка текста |
| `overlay_render.hpp` | 477 | 21 074 Б | `wglSwapBuffers`, `HUD_Redraw`, `V_CalcRefdef`, рендер субтитров и глав |
| `main.cpp` | 464 | 20 007 Б | Перехваты `hw.dll`/`client.dll`/`opengl32.dll`, компактная броня, обход ПНВ |
| `flashlight_gl.hpp` | 463 | 20 586 Б | Объёмный OpenGL 3D-фонарик с инерцией |
| `config.hpp` | 434 | 21 893 Б | Парсер `subtitles.ini`, hot-reload, роли и цвета |
| `app_state.hpp` | 371 | 12 947 Б | Общее состояние, `SRWLOCK`, определение игры |
| `hud_fbo_scaler.hpp` | 256 | 11 685 Б | FBO-захват и масштабирование HUD без перекрытий |
| `goldsrc.hpp` | 237 | 9 408 Б | Структуры и ABI движка GoldSrc |
| `text_layout.hpp` | 227 | 9 804 Б | Перенос строк, пагинация и расчёт геометрии плашки |
| `database.hpp` | 202 | 7 384 Б | Загрузка TSV-баз и нормализация ключей |
| `hook_x86.hpp` | 172 | 7 092 Б | x86 inline-трамплины и поиск по xref-строкам |
| `version_proxy.cpp` | 126 | 4 733 Б | Win32-прокси `version.dll` и точка входа |
| `version.def` | 19 | 909 Б | Таблица экспортов `version.dll` |
| `build.bat` | 9 | 368 Б | Скрипт сборки под `i686-w64-mingw32` |

### Базы субтитров и заголовков (2 818 ключей на язык + 129 заголовков)
| Файл | Записей | Размер |
| :--- | ---: | ---: |
| `valve/subtitles_en.tsv` | 1 806 | 145 572 Б |
| `valve/subtitles_ru.tsv` | 1 806 | 236 275 Б |
| `valve/titles_ru.tsv` | 66 | 7 237 Б |
| `bshift/subtitles_en.tsv` | 455 | 48 178 Б |
| `bshift/subtitles_ru.tsv` | 455 | 80 277 Б |
| `bshift/titles_ru.tsv` | 28 | 2 290 Б |
| `gearbox/subtitles_en.tsv` | 557 | 49 902 Б |
| `gearbox/subtitles_ru.tsv` | 557 | 75 714 Б |
| `gearbox/titles_ru.tsv` | 35 | 3 399 Б |

### Артефакты и документация
| Файл | Размер |
| :--- | ---: |
| `version.dll` | 1 812 480 Б |
| `release/HLCC-win32.zip` | 841 202 Б |
| `subtitles.ini` | 5 037 Б |
| `MODDER_GUIDE.md` | 6 986 Б |
| `TRANSLATION_GUIDE.md` | 4 896 Б |
| `README.txt` | 1 832 Б |
