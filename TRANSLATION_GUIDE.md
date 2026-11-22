# Subtitle Translation Guide (HLCC)

This guide explains how to translate HLCC subtitles and HUD titles into another language for **Half-Life (`valve`)**, **Blue Shift (`bshift`)**, and **Opposing Force (`gearbox`)**.

---

## 1. Files to Translate

Each game directory contains its own TSV files:

| Game | Subtitles Source (English) | HUD Titles / Chapters Reference |
| :--- | :--- | :--- |
| Half-Life (`valve`) | `valve/subtitles_en.tsv` (1,806 keys) | `valve/titles_ru.tsv` (67 keys) |
| Blue Shift (`bshift`) | `bshift/subtitles_en.tsv` (455 keys) | `bshift/titles_ru.tsv` (29 keys) |
| Opposing Force (`gearbox`) | `gearbox/subtitles_en.tsv` (557 keys) | `gearbox/titles_ru.tsv` (36 keys) |

> **Note:** Because `subtitles.ini` sets `fallback_dir=valve`, shared Half-Life lines (such as standard scientists, guards, HECU, HEV, and SFX) are automatically inherited by `bshift` and `gearbox`. You do **not** need to duplicate `valve` entries inside `bshift` or `gearbox`.

---

## 2. File Format & Encoding Rules

### Encoding
- Save all files as **UTF-8 without BOM**.
- Standard LF (`\n`) or CRLF (`\r\n`) line endings are both supported.
- Never leave literal newlines inside a single entry.

### `subtitles_<lang>.tsv` Format
Each non-empty line has **3 tab-separated (`\t`) columns**:
```text
KEY<TAB>DURATION_MS<TAB>TEXT
```
Example (`subtitles_en.tsv` → `subtitles_de.tsv`):
```tsv
ba_tram0	2500	[Security] Good morning, Calhoun.
```
```tsv
ba_tram0	2500	[Sicherheit] Guten Morgen, Calhoun.
```

Rules:
- **Column 1 (`KEY`)**: Keep **exactly** as in `subtitles_en.tsv`. Do not translate, lowercase, or rename keys.
- **Column 2 (`DURATION_MS`)**: Keep the original millisecond duration (e.g., `2500`). `0` means auto-calculated from text length.
- **Column 3 (`TEXT`)**: Your translated text. Must be non-empty and contain no tab characters.

### `titles_<lang>.tsv` Format
Each non-empty line (excluding `#` comment lines) has **2 tab-separated (`\t`) columns**:
```text
KEY<TAB>TEXT_WITH_\n
```
- Use literal `\n` (two characters: backslash + `n`) for line breaks in multi-line intro/outro cards:
  ```tsv
  CR28	SUBJECT:\nGordon Freeman\nMale, age 27
  T0A0TITLE	HAZARD COURSE
  ```

---

## 3. Speaker Tags, SFX, and Formatting

- **Spoken lines** begin with a speaker tag in square brackets, e.g.:
  - `[Scientist]`, `[Security]`, `[HECU]`, `[Black Ops]`, `[Otis]`, `[Rosenberg]`, `[G-Man]`, `[HEV]`, `[Announcement]`, `[Drill Instructor]`, `[Radio]`
  - Translate the speaker tag consistently across all three games, keeping the `[...]` brackets intact.
- **Sound effects (SFX)** are enclosed entirely in brackets:
  - `[Headcrab screech]`, `[Metal door opens]`, `[Explosion]`
  - Translate the description inside `[...]`.
- **Role colors** are assigned automatically in `subtitles.ini` based on the **sound key** (not the translated `[Speaker]` text), so colors will continue working automatically in any language.
- **Keep lines concise** where possible: `default` style paginates long lines automatically, while `hl2` style scrolls inside a 4-line box.

---

## 4. Testing Your Translation In-Game

1. Place your translated files in the corresponding game folders (`valve/`, `bshift/`, `gearbox/`), for example `subtitles_de.tsv` and `titles_de.tsv`.
2. Open `subtitles.ini` in the Half-Life root directory and update the `[Files]` section:
   ```ini
   [Files]
   language=auto
   fallback_dir=valve
   subtitles_pattern=subtitles_de.tsv
   titles_pattern=titles_de.tsv
   ```
3. Launch the game in **OpenGL** mode (or if already running, save `subtitles.ini` — it hot-reloads within ~1 second, or run `subtitles_reload` in the developer console).

---

## 5. Validation Before Release

Before sharing your translation, verify that:
1. Every key in `subtitles_en.tsv` exists in your `subtitles_<lang>.tsv` (1:1 key parity, no duplicates).
2. Every row has exactly 3 tab-separated columns and a valid integer in column 2.
3. No encoding errors (`\uFFFD` replacement characters) or UTF-8 BOM are present.

Quick Python validation check (run from the mod root folder):
```python
python -c "
import os
lang = 'de'  # change to your language code
for mod in ['valve', 'bshift', 'gearbox']:
    en = {l.split('\t')[0] for l in open(f'{mod}/subtitles_en.tsv', encoding='utf-8') if l.strip()}
    raw = open(f'{mod}/subtitles_{lang}.tsv', 'rb').read()
    assert not raw.startswith(b'\xef\xbb\xbf'), f'{mod}: remove UTF-8 BOM'
    ru = {}
    for i, l in enumerate(raw.decode('utf-8').splitlines(), 1):
        if not l.strip(): continue
        p = l.split('\t')
        assert len(p) == 3 and p[1].isdigit() and p[2].strip(), f'{mod}:{i} invalid row'
        assert p[0] not in ru, f'{mod}:{i} duplicate key {p[0]}'
        ru[p[0]] = True
    assert en == set(ru), f'{mod}: key diff en-only={en-set(ru)} new-only={set(ru)-en}'
    print(f'{mod}: OK ({len(ru)} keys)')
"
```
