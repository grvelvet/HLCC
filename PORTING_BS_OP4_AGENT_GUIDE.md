# Инструкция для агента по портированию HLCC на Half-Life: Blue Shift (`bshift`) и Opposing Force (`gearbox`)

Этот документ — пошаговое техническое задание для ИИ-агента по адаптации движка субтитров **HLCC** (`C++23`, `version.dll`) под официальные аддоны **Half-Life: Blue Shift** и **Half-Life: Opposing Force**.

---
## 0. Правила экономичной работы агента (экономия контекста и шагов)

1. **Не читать целиком большие и неизменяемые файлы:**
   - Никогда не открывать `src/clacon_font.hpp` (`1.89 МБ`), `src/version_proxy.cpp`, `src/version.def`.
   - `src/main.cpp` (~`1 600` строк) и `valve/subtitles_*.tsv` (`1 663` строки) читать только точечно (по `50–120` строк после поиска нужной функции через `rg -n` / `Select-String`).
2. **Объединять анализ в один Python-скрипт:**
   - Diff `sentences.txt` аддона против `valve/`, поиск новых `.wav`, парсинг `scripted_sentence` / `ambient_generic` из `maps/*.bsp` и расчёт `duration_ms` из RIFF/WAV делать одним скриптом, выводя в консоль только сводку и список недостающих ключей.
3. **Не дублировать базу `valve/`:**
   - Программно вычитать ключи `valve/subtitles_ru.tsv` перед генерацией `bshift/` и `gearbox/` — переводить только дельту новых/изменённых реплик.
4. **Проверка прологов DLL за 1 команду:**
   - Прологи экспортов `bshift\cl_dlls\client.dll` и `gearbox\cl_dlls\client.dll` (`Initialize`, `HUD_Redraw`, `V_CalcRefdef`, `HUD_Reset`) проверять одним коротким Python-скриптом по таблице экспортов PE (первые 16 байт).
5. **Автовалидация и параллельный перевод:**
   - При >150 новых реплик разбивать на 2–4 чанка для параллельного перевода, затем одним скриптом проверить TSV (ровно 3 колонки `\t`, валидный UTF-8, 100% совпадение ключей и `duration_ms` между `RU` и `EN`, `0` кириллицы в `EN`) и сразу собрать `build.bat`.

## 1. Что уже совместимо «из коробки»

Базовая архитектура `HLCC` (`version.dll` + OpenGL-рендер + хуки `hw.dll`/`client.dll`) рассчитана на любой мод GoldSrc:
- `database.hpp` уже читает `fallback_dir` из `liblist.gam` и загружает базы в порядке:
  `valve/` → `fallback_dir` → `<active_game_dir>/` (`bshift/` или `gearbox/`).
- Повторно переводить `~1 663` общих реплик из `valve/subtitles_*.tsv` **не нужно** — в `bshift/` и `gearbox/` достаточно положить **только новые и переопределённые** реплики, а всё общее подтянется из `valve/`.
- Формат хранения остаётся прежним: UTF-8 TSV (`key<TAB>duration_ms<TAB>text`).

---

## 2. Обязательные проверки и правки в коде (`src/`)

### Шаг 2.1. Отключить `restore_unused_hev` для `bshift` и `gearbox` (`main.cpp`)
В **Blue Shift** (Барни) и **Opposing Force** (Шепард с бронежилетом PCV) **нет костюма HEV Mark IV** и его голосовой подсистемы.
- В `ReloadAllFromDisk()` (рядом с `g_isMModGame`) добавить проверку:
  ```cpp
  bool g_isAddonWithoutHev = (gdLower == "bshift" || gdLower == "gearbox");
  ```
- В `TryTriggerRestoredHev()` и `OnWeapPickup()` блокировать восстановленные фразы HEV при `g_isMModGame || g_isAddonWithoutHev`.
- Иначе при подборе бронежилета (`Battery`) или оружия в `bshift`/`gearbox` будут ошибочно срабатывать `!HEV_D00` и `!HEV_PISTOL`.

### Шаг 2.2. Проверить хуки `client.dll` в `bshift` и `gearbox`
У `bshift\cl_dlls\client.dll` и `gearbox\cl_dlls\client.dll` другие сборки по сравнению с `valve`:
1. Проверить первые 16 байт экспортов `Initialize`, `HUD_Redraw`, `V_CalcRefdef`, `HUD_Reset` — убедиться, что `hook::InsnLen32` и `InstallInlineHook` в `hook_x86.hpp` корректно декодируют их прологи.
2. Проверить `FindClientEngfuncs(pInit)`: в `valve` ищет байты `BF <addr> F3 A5` в первых 64 байтах. Если в `gearbox`/`bshift` таблица `cl_enginefunc_t` копируется иначе (например, другим смещением или инструкцией), расширить поиск или добавить паттерн.
3. Проверить работу `pfnTextMessageGet` и `HUD_Redraw` (получение `GetClientTime()` для паузы и `SCREENINFO`).

### Шаг 2.3. Добавить новые роли в `[Colors]` и `[Roles]` (`config.hpp` и `subtitles.ini`)
По умолчанию неизвестные префиксы красятся в `color_default`. Добавить поддержку новых персонажей аддонов:
- **Opposing Force (`gearbox`):**
  - `otis/`, `ot_` — охранник Отис (`color_guard`)
  - `fgrunt/`, `fg_` — дружественные солдаты HECU (выделить отдельным цветом или `color_guard` / `color_ally`, чтобы отличать от врагов!)
  - `drill/`, `di_` — сержант-инструктор (`color_guard` или `color_drill`)
  - `medic/` — полевой медик (`color_guard`)
  - `cleansuit/`, `cs_` — учёные в химзащите (`color_scientist`)
  - `radio/` / `opfor_radio` — полевое радио (`color_vox` + категория `vox` для курсива в `default`)
- **Blue Shift (`bshift`):**
  - `rosenberg/`, `ro_`, `simmons/`, `walter/` — учёные (`color_scientist`)
  - `otis/`, `ba_` — охрана (`color_guard`)

### Шаг 2.4. Проверить исключения дальности и стен (`HandleSound` в `main.cpp`)
Проверить по картам `bshift/maps/*.bsp` и `gearbox/maps/*.bsp` реплики, которые звучат:
- **через стекло или решётки** (увеличить радиус / отключить `IsSoundOccluded`, по аналогии с `isSiloSci`);
- **по рации, громкоговорителю или в скриптовых сценах на дистанции `> 650` юнитов** (по аналогии с `ambientSpecial` и `isScriptedBarney`);
- **вступительные поездки/вертолёт** в `gearbox` (`of0a0` / `of1a1`, радиопилот/командир в `Osprey`) и **вагонетка/лифт** в `bshift` (`ba_tram*`).

---

## 3. Пайплайн сбора базы субтитров и заголовков

Для каждого аддона (`bshift` и `gearbox`) выполнить по порядку:

1. **Сравнить `sentences.txt` с `valve/sound/sentences.txt`:**
   - Выделить все новые и изменённые ключи предложений.
   - Учесть префиксы групп (`FG_`, `OT_`, `DR_`, `RO_`, `BS_`, `OF_` и т.д.).

2. **Просканировать все аудиофайлы `sound/` и карты `maps/*.bsp`:**
   - Найти все `.wav` в речевых директориях аддона, отсутствующие в `valve/`.
   - Распарсить сущности `scripted_sentence` и `ambient_generic` из всех `maps/*.bsp`, чтобы не пропустить прямые ссылки на `.wav` (минуя `sentences.txt`) и оценить дистанции/стены.

3. **Рассчитать точные длительности (`duration_ms`):**
   - Для одиночных `.wav`: вычислить точную длительность из заголовка RIFF/WAV (или фактических сэмплов).
   - Для составных фраз из `sentences.txt`: суммировать длительности всех `.wav`-фрагментов с учётом модификаторов `(p...)`, `(e...)`, `(t...)`, `(s...)` и пауз `,` / `.`.

4. **Подготовить `subtitles_ru.tsv` и `subtitles_en.tsv`:**
   - Строгий формат: `ключ<TAB>длительность_мс<TAB>[Спикер] Текст`.
   - Проверить консистентность тегов (`[Учёный]`/`[Scientist]`, `[Охранник]`/`[Security]`, `[Отис]`/`[Otis]`, `[Инструктор]`/`[Drill Instructor]`, `[HECU]`, `[Система оповещения]`/`[Announcement]` и т.д.).
   - Проверить перенос длинных фраз (`||` при необходимости).

5. **Подготовить заголовки глав (`titles_ru.tsv`):**
   - Извлечь ключи из `bshift/titles.txt` и `gearbox/titles.txt`.
   - Записать в `bshift/titles_ru.tsv` и `gearbox/titles_ru.tsv` (`КЛЮЧ<TAB>ТЕКСТ`), проверив, чтобы при `language=en` они не подменяли оригинальные английские титры.

6. **Меню (опционально):**
   - Добавить `resource/gameui_russian.txt` и `resource/valve_russian.txt` (или `bshift_russian.txt` / `gearbox_russian.txt`) в соответствующие папки аддонов.

---

## 4. Чек-лист верификации перед релизом

1. **Статическая проверка TSV:**
   - Ровно 3 колонки (`\t`) в каждой строке `subtitles_*.tsv`, валидный UTF-8 без битых символов.
   - 100% совпадение ключей и `duration_ms` между `subtitles_ru.tsv` и `subtitles_en.tsv`.
   - Все новые ключи из `sentences.txt` и `maps/*.bsp` присутствуют в TSV (либо в `valve/`).
2. **Сборка:**
   - Пересобрать `version.dll` (`build.bat` с `-std=c++23 -fno-exceptions`), прогнать юнит-тесты `LoadIni` / `LoadSubtitlesTsv` / `Classify`.
3. **Проверка в игре (`debug_log=1` → `subtitles_debug.log`):**
   - Запустить `hl.exe -game bshift` и `hl.exe -game gearbox` (в режиме OpenGL).
   - Убедиться, что:
     - нет вылетов на старте/загрузке карт;
     - `restore_unused_hev` молчит (не звучит голос HEV при подборе брони/оружия);
     - отображаются заголовки глав (белым полупрозрачным `Trebuchet MS`);
     - в `subtitles_debug.log` нет пропущенных (`missing`) речевых ключей на ключевых стартовых сценах (`ba_tram1`, `ba_security1`, `of0a0`, `of1a1`) и в бою с новыми NPC.

---

## 5. История правок и проверенные решения для `bshift` (на базе опыта `gearbox` и `valve`)

Чтобы при портировании **Blue Shift (`bshift`)** не проходить заново отладку, ниже зафиксировано всё, что уже внедрено в `version.dll`, и какие грабли были найдены на `gearbox`:

### 5.1. Что уже готово в `version.dll` и `subtitles.ini`
- **`restore_unused_hev` уже отключён для `bshift` и `gearbox`:** в `main.cpp` есть `g_isAddonWithoutHev`.
- **Роли `bshift` уже прописаны в `config.hpp` и `subtitles.ini`:** `rosenberg/`, `ro_`, `simmons/`, `walter/`, `otis/`, `ot_`, `cleansuit/`, `cs_`.
- **Изоляция HUD:** HiDPI HUD из `gearbox` включается строго по `g_isGearboxGame` (`gdLower == "gearbox"`), поэтому `valve` и `bshift` не ломаются. (Если для `bshift` понадобится свой синий HUD — заводить отдельный флаг `g_isBshiftGame`, не смешивать с `valve`.)
- **Контекстные звуки (SFX) в стиле HL2:** в `config.hpp` / `main.cpp` добавлены `RoleCat::SFX` (`sfx`) и `RoleCat::SFX_DANGER` (`sfx_danger`), курсив в пресете `default`, ограничение дистанции (`sfx_max_distance=550`), громкости (`sfx_min_volume=0.20`), кулдаун (`sfx_cooldown_ms=2500`) и лимит `max_sfx_captions=2` без вытеснения активной речи.
- **База `valve/` уже содержит 146 Tier-1 SFX-записей** — они автоматически подтянутся в `bshift` через `fallback_dir=valve`.

### 5.2. Найденные баги и обязательные проверки для `bshift`
1. **Контекстная сепарация субтитров по аддонам (`valve` / `gearbox` / `bshift`):**
   - `database.hpp` загружает `valve/` как общий fallback, а поверх — папку активного мода (`gearbox/` или `bshift/`). Моды **не пересекаются** друг с другом: всё специфичное для Opposing Force (броня PCV, HECU, боссы `gearbox`) живёт только в `gearbox/`, а всё специфичное для Blue Shift — только в `bshift/`.
   - **Разделение контекста оборудования и реплик:** даже если `.wav` физически лежит в `valve/sound/`, но в аддоне имеет другой смысл, спикера или лор, ключ **обязательно** переопределяется в `subtitles_*.tsv` этого аддона (пример: `items/suitcharge*` в `gearbox` переопределён на `[Зарядка брони]` для PCV Шепарда, тогда как в `valve` остаётся `[Зарядка костюма]` для HEV Фримена; в `bshift` у Барни энергоброни вообще нет — только обычный бронежилет/шлем и медстанции `medcharge4`).
   - То же касается пересекающихся ключей `ba_*`, `sc_*`, `c1a0_*` и уникальных SFX: каждый аддон переопределяет в своей папке только свой контекст, не меняя файлы `valve/` и не протекая в другие аддоны (HUD и модовая логика в C++ тоже строго разделяются по `gdLower == "gearbox"` / `"bshift"`).
2. **Не добавлять стартовые звуки зарядников (`suitchargeok1`, `medshot4`):**
   - В GoldSrc при нажатии `E` на зарядник `items/suitchargeok1` проигрывается **до** `items/suitcharge1`. Если его подписать как `[Костюм заряжен]` / `[Броня заряжена]`, игрок сначала видит «заряжен», а потом «зарядка». Стартовые звуки (`suitchargeok1`, `medshot4`) в TSV не добавлять.
3. **Не добавлять спам-SFX (только Tier 1):**
   - Исключать двери (`doors/`), кнопки (`buttons/`), искры (`spark*`), разрушение ящиков/стёкол (`debris/`), фонарик (`flashlight*`), подбор патронов/предметов (`ammopickup*`, `gunpickup*`), фазы `idle`/`pain`/`die`/`eat` обычных мобов. Оставлять только взрывы, мины, турели, сирены/тревоги, счётчик Гейгера, зарядники, запертые замки и `alert`/спецатаки врагов.
4. **Короткие и единые имена спикеров:**
   - Использовать чистые теги без лишних приставок (`[Охранник]`, `[Отис]`, `[Учёный]`, `[Розенберг]`, `[Медик]`, `[Солдат]`, `[Система оповещения]`).
5. **Технические проверки перед сборкой `bshift`:**
   - Проверить прологи экспортов `bshift/cl_dlls/client.dll` (`Initialize`, `HUD_Redraw`, `V_CalcRefdef`, `HUD_Reset`) и паттерн `FindClientEngfuncs`.
   - Просканировать `bshift/maps/*.bsp` на реплики за стеклом или на дистанции `> 650` юнитов (`ba_tram*`, `ba_security*`, Розенберг у телепорта/в камере) и при необходимости добавить исключения в `HandleSound`.
   - Все записи `bshift` (речь + переопределения + уникальные SFX) класть в `bshift/subtitles_ru.tsv` и `bshift/subtitles_en.tsv` (проверяя 3 колонки, UTF-8, `0` кириллицы в EN, 100% совпадение ключей и `duration_ms`).
   - В Python-скриптах никогда не совмещать `open(p, "w").write(open(p).read()...)` — всегда сначала читать файл целиком в переменную (`Path.read_text()`), затем записывать (`Path.write_text()`).
