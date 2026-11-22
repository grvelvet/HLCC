# HLCC — Half-Life Closed Captions (C++23)

[Русский](README.md) | **English**

A proxy plugin mod (`version.dll`) for the **Half-Life**, **Opposing Force**, and **Blue Shift** Steam trilogy without modifying original game files:

- **HL2-Style Closed Captions:** NPC dialogue, radio, VOX, suit (HEV/PCV), and key gameplay SFX with anti-spam protection.
- **Distance & Occlusion Filtering:** filters out sounds behind solid walls while handling glass and scripted scenes properly.
- **Two OpenGL Render Styles:** dynamic plate (`default`) or fixed-width box with smooth vertical scrolling (`hl2`).
- **Auto-Localization (EN/RU):** automatic language selection from Steam settings, chapter/title translation, and optional `.wav` auto-timing for third-party voiceovers.
- **HUD & Graphics Enhancements:** FBO HUD scaling and compact armor layout for the expansions (`bshift`, `gearbox`), plus a 3D projected OpenGL flashlight.

## Installation (from GitHub Releases)
1. Download `HLCC-win32.zip` from the **Releases** page.
2. Extract its contents into the **Half-Life** root directory (next to `hl.exe`), overwriting if prompted.
3. Launch the game in **OpenGL** mode.
4. **For Linux / Steam Deck (Steam Proton):** enable **Proton** in the game's Steam Compatibility properties (the native Linux `hl_linux` binary does not load `version.dll`) and add to Launch Options:
   ```text
   WINEDLLOVERRIDES="version=n,b" %command%
   ```
5. Optionally customize the font, preset, colors, `[Mod:*]` profiles, and filters in `subtitles.ini`.

## Documentation
- [MODDER_GUIDE.md](MODDER_GUIDE.md) — `version.dll` architecture, sound/HUD pipelines, and adapting HLCC to third-party GoldSrc mods
- [TRANSLATION_GUIDE.md](TRANSLATION_GUIDE.md) — guide for translating subtitles and HUD titles into other languages

## Repository Structure
- `src/` — `version.dll` C++23 source code and `build.bat`
- `valve/` — subtitle (`subtitles_ru.tsv`, `subtitles_en.tsv`) and title (`titles_ru.tsv`) databases for Half-Life
- `gearbox/` — databases for Opposing Force
- `bshift/` — databases for Blue Shift
- `subtitles.ini` — default configuration (including `[Mod:bshift]`, `[Mod:gearbox]`, `[Mod:hl1mmod]` profiles)
- `package_release.ps1` — packages `release/HLCC-win32.zip`

## Building from Source
Requires **LLVM-MinGW (UCRT)** (`winget install MartinStorsjo.LLVM-MinGW.UCRT`) targeting `i686-w64-mingw32`:

```bat
cd src
build.bat
```

The compiled `version.dll` is output to the repository root. To package the release archive:

```powershell
powershell -ExecutionPolicy Bypass -File package_release.ps1
```

## GitHub Actions CI
`.github/workflows/build.yml` runs on `windows-latest` with **LLVM-MinGW (UCRT)**:
- On any `push`, `pull_request`, or `workflow_dispatch`, it validates the TSV databases, builds `version.dll` via `src/build.bat`, and uploads the `HLCC-win32` artifact.
- Pushing a `v*` tag (e.g., `git tag v1.0b && git push origin v1.0b`) automatically publishes a **GitHub Release** with `HLCC-win32.zip`.
