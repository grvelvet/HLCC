# HLCC Architecture & Modder Guide

This document explains how **HLCC (Half-Life Closed Captions)** is structured internally, how its hooks interact with the GoldSrc engine, and how to extend or adapt it for other Half-Life mods.

---

## 1. High-Level Architecture

HLCC is compiled as a 32-bit `version.dll` proxy placed next to `hl.exe`:

1. **DLL Proxy (`version_proxy.cpp`, `version.def`)**  
   Forwards all standard Win32 `version.dll` exports to `%SystemRoot%\System32\version.dll` and calls `InitMod()` on `DLL_PROCESS_ATTACH`.
2. **Dynamic Hook Installation (`main.cpp`, `hook_x86.hpp`)**  
   Hooks `LoadLibraryA` to detect when `hw.dll`, `client.dll`, and `opengl32.dll` load (or attaches immediately if already loaded):
   - **`hw.dll`** (via string-xref scan): `S_StartDynamicSound`, `S_StartStaticSound`, `Host_Loadgame_f`.
   - **`client.dll`** (via `GetProcAddress` + `cl_enginefunc_t` slot patching): `Initialize`, `HUD_Redraw`, `V_CalcRefdef`, `HUD_Reset`, `pfnTextMessageGet`, `pfnHookUserMsg`, `pfnSPR_*`, `pfnFillRGBA`, `pfnDrawCharacter`, `pTriAPI->Begin`/`End`, `pEfxAPI->CL_AllocDlight`.
   - **`opengl32.dll`**: `wglSwapBuffers`.
3. **Per-Frame Execution Flow**
   - `V_CalcRefdef` captures camera position/angles (`g_viewOrg`, `g_viewAngles`) for distance/occlusion checks and updates flashlight sway.
   - `S_StartDynamicSound` / `S_StartStaticSound` feed `HandleSound()` to filter, classify, and enqueue active captions (local captions smoothly fade out if the player moves far beyond `effMaxDist * 1.35`).
   - `HUD_Redraw` renders the 3D GL flashlight (when enabled, resolving MSAA FBO depth via `glBlitFramebuffer`), then captures the vanilla sprite HUD into an offscreen RGBA8 FBO (`g_hudFbo` when `hud_scale > 1.01`) and compacts Armor closer to Health (`CompactHudAdjustX` when `hud_compact_armor=1`). Full-screen `FillRGBA` / `TriAPI` draws (such as Night Vision) and `pfnDrawCharacter` (`titles.txt` credits/messages) temporarily pause FBO capture so they render directly to the backbuffer.
   - `wglSwapBuffers` composites the scaled HUD FBO (`CompositeScaled`) and draws the subtitle/title overlay (`RenderSubtitlesOverlay`).

---

## 2. Source Code Map (`src/`)

| File | Responsibility |
| :--- | :--- |
| `version_proxy.cpp`, `version.def` | Win32 `version.dll` forwarder and `DllMain` entry point. |
| `hook_x86.hpp` | x86 inline 5-byte `JMP` trampolines and string-xref scanner for `hw.dll`. |
| `goldsrc.hpp` | Engine ABI structs (`cl_enginefunc_t`, `triangleapi_t`, `efx_api_t`, `event_api_t`, `ref_params_t`, `sfx_t`, `pmtrace_t`, `dlight_t`, `VGuiSurface`). |
| `main.cpp` | Hook installation, `cl_enginefunc_t` patching, user-message hooks (`Health`, `Battery`, `Damage`, `WeapPickup`, `Flashlight`), compact armor layout, and FBO bypasses. |
| `app_state.hpp` | Thread-safe global state (`SRWLOCK`), active mod directory & `liblist.gam` fallback detection, and `ReloadAllFromDisk()`. |
| `config.hpp` | `subtitles.ini` parser, ~1s hot-reload, `[Mod:<gameDir>]` profile overrides, role/color matching, `no_occlusion_prefixes`, `sfx_creature_groups`, and `[HUD]` options. |
| `database.hpp` | UTF-8 TSV loader (`subtitles_*.tsv`, `titles_*.tsv`), `fallback_dir` inheritance, and key normalization (strips `!`, `*`, `#`, `sound/`, `.wav`). |
| `sound_handler.hpp` | Caption pipeline: stop/replace handling, role filtering, distance check, 2-height `EV_PlayerTrace` occlusion (`PM_GLASS_IGNORE`), SFX cooldowns, and HEV restore cues. |
| `text_layout.hpp`, `gl_font.hpp` | Word wrapping, pagination, GDI-backed OpenGL glyph atlas, and rounded-plate rendering (`default` and `hl2` styles). |
| `hud_fbo_scaler.hpp` | Offscreen RGBA8 FBO HUD capture and non-overlapping corner-anchored scaling (`GL_ONE, GL_ONE` additive composite). |
| `flashlight_gl.hpp` | Projected OpenGL 3D flashlight (`GlFlashlight`) with inertial sway and optional vanilla `CL_AllocDlight` suppression. |
| `overlay_render.hpp` | `Hook_WglSwapBuffers`, `Hook_HUD_Redraw`, `Hook_V_CalcRefdef`, `Hook_HUD_Reset`, and `Hook_TextMessageGet` (chapter title localization). |

---

## 3. Sound & Subtitle Pipeline Details

1. **Key Normalization (`database.hpp`)**:  
   Engine sound names arrive as `!SC_HELLO`, `scientist/hello.wav`, `*vox/bizwarn.wav`, etc. `Database::NormalizeKey` lowercases and strips leading `!`/`*`/`#`, `sound/`, and trailing `.wav`.
2. **Lookup Order**:  
   `fallback_dir` (default `valve`) is loaded first, then the active mod directory (`bshift`, `gearbox`, or custom mod) overrides matching keys.
3. **Occlusion & Scripted Scenes (`sound_handler.hpp`, `config.hpp`)**:
   - Sounds normally undergo distance (`max_distance`) and 2-height line-of-sight (`IsSoundOccluded`) checks, with `PM_GLASS_IGNORE` within `glass_max_distance`.
   - Keys matching `no_occlusion_prefixes` bypass occlusion and get extended range (`effMaxDist`). Use this in `subtitles.ini` for scripted scenes behind locked doors, glass, or walls.
   - Keys in `ambientSpecial` (e.g., `tride/`, `nihilanth/`, `intro/`, `intro_`, `radio/`, `rd_`) are treated as global (`isGlobal = true`).

---

## 4. HUD Scaling, Compact Armor & `[Mod:<gameDir>]` Profiles

- **Data-Driven Mod Profiles (`[Mod:<gameDir>]` in `subtitles.ini`)**:  
  Global defaults leave the HUD untouched (`hud_compact_armor=0`, `hud_scale=1.0`) so vanilla `valve` and unknown mods stay clean. Per-mod overrides (`[Mod:bshift]`, `[Mod:gearbox]`, `[Mod:hl1mmod]`) control `hud_scale`, `hud_compact_armor`, `enable_gl_flashlight`, `enable_hev`, `restore_unused_hev`, and `no_occlusion_max_distance` without hardcoding mod names in C++.
- **Compact Armor (`hud_compact_armor=1`)**:  
  `CompactHudAdjustX` in `main.cpp` tracks the rightmost X of Health sprites and leftmost X of Armor sprites in `HUD_Redraw`, shifting Armor closer to Health on widescreen resolutions (`>= 800px`).
- **FBO HUD Scaling (`hud_scale=1.5`)**:  
  `HudFboScaler` captures `HUD_Redraw` into an RGBA8 FBO and composites 5 scaled corner/pickup-strip regions (Bottom-Left, Bottom-Right, Top-Left, Top-Right, Middle-Right pickup history) plus 5 non-overlapping 1:1 unscaled center strips.  
  - Full-screen `FillRGBA` (`width >= g_screenW / 2` or `>= 64x64`), `pTriAPI->Begin`/`End`, and `pfnDrawCharacter` (`titles.txt` credits/messages) pause FBO capture so full-screen tints (Night Vision) and credits never get clipped or doubled.

---

## 5. Adapting HLCC to Another GoldSrc Mod

1. **Add Subtitles Without Recompiling**:
   - Create `<mod_dir>/subtitles_en.tsv` (and/or `subtitles_ru.tsv`, `titles_ru.tsv`).
   - Keep `fallback_dir=valve` in `subtitles.ini` so base Half-Life sounds are inherited automatically.
   - Add any mod-specific speaker prefixes under `[Roles]` and behind-wall scripted scenes under `no_occlusion_prefixes=` in `subtitles.ini`.
2. **Debugging Sound Keys In-Game**:
   - Set `debug_log=1` in `subtitles.ini` and inspect `subtitles_debug.log` in the Half-Life root folder to see raw/normalized keys, `[sfx-miss]` missing speech/sentence keys, distances, occlusion skips, and hook addresses.
   - Use `subtitles_reload` in the developer console after editing `.ini` or `.tsv` files.

---

## 6. Building & Packaging

- **Toolchain**: `LLVM-MinGW (UCRT)` (`winget install MartinStorsjo.LLVM-MinGW.UCRT`), 32-bit target `i686-w64-mingw32`.
- **Build `version.dll`**:
  ```bat
  cd src
  build.bat
  ```
  Compiles with `-std=c++23 -fno-exceptions -O2 -shared` and statically links `libgcc` / `libstdc++` / `winpthread`.
- **Package Release Zip**:
  ```powershell
  powershell -ExecutionPolicy Bypass -File package_release.ps1
  ```

---

## 7. Subtitle & Title Translation Guide

### Files to Translate

| Game | Subtitles Source (English) | HUD Titles Reference |
| :--- | :--- | :--- |
| Half-Life (`valve`) | `valve/subtitles_en.tsv` (1,806 keys) | `valve/titles_ru.tsv` (66 keys) |
| Blue Shift (`bshift`) | `bshift/subtitles_en.tsv` (455 keys) | `bshift/titles_ru.tsv` (28 keys) |
| Opposing Force (`gearbox`) | `gearbox/subtitles_en.tsv` (557 keys) | `gearbox/titles_ru.tsv` (35 keys) |

> Because `subtitles.ini` sets `fallback_dir=valve`, shared Half-Life lines are automatically inherited by `bshift` and `gearbox` and do not need to be duplicated.

### TSV Format & Encoding Rules
- Save all files as **UTF-8 without BOM** (LF or CRLF line endings).
- **`subtitles_<lang>.tsv`**: 3 tab-separated (`\t`) columns — `KEY<TAB>DURATION_MS<TAB>TEXT`.
  - Keep `KEY` and `DURATION_MS` unchanged (`0` = auto-calculated duration). Never put literal tabs or newlines inside `TEXT`.
- **`titles_<lang>.tsv`**: 2 tab-separated (`\t`) columns — `KEY<TAB>TEXT_WITH_\n` (use literal `\n` for multi-line cards; `#` lines are comments).

### Speaker & SFX Tags
- **Spoken lines** begin with a bracketed speaker tag (e.g. `[Scientist]`, `[Security]`, `[HECU]`, `[Black Ops]`, `[Otis]`, `[Rosenberg]`, `[G-Man]`, `[HEV]`, `[Announcement]`). Translate the label consistently while keeping `[...]`.
- **Sound effects (SFX)** are wrapped completely in brackets (e.g. `[Headcrab screech]`).
- **Role colors** are matched by the **sound key** (not the translated tag text), so colors work in any language automatically.

### Testing & Validation
1. Place `subtitles_<lang>.tsv` and `titles_<lang>.tsv` in `valve/`, `bshift/`, `gearbox/`, and update `[Files]` in `subtitles.ini`:
   ```ini
   [Files]
   language=auto
   fallback_dir=valve
   subtitles_pattern=subtitles_de.tsv
   titles_pattern=titles_de.tsv
   ```
2. Run `subtitles_reload` in the console (or wait ~1s for hot-reload).
3. Validate 1:1 key parity and encoding before release:
   ```python
   python -c "
   lang = 'de'
   for mod in ['valve', 'bshift', 'gearbox']:
       en = {l.split('\t')[0] for l in open(f'{mod}/subtitles_en.tsv', encoding='utf-8') if l.strip() and not l.lstrip().startswith('#')}
       raw = open(f'{mod}/subtitles_{lang}.tsv', 'rb').read()
       assert not raw.startswith(b'\xef\xbb\xbf'), f'{mod}: remove UTF-8 BOM'
       tr = {}
       for i, l in enumerate(raw.decode('utf-8').splitlines(), 1):
           if not l.strip() or l.lstrip().startswith('#'): continue
           p = l.split('\t')
           assert len(p) == 3 and p[1].isdigit() and p[2].strip(), f'{mod}:{i} invalid row'
           assert p[0] not in tr, f'{mod}:{i} duplicate key {p[0]}'
           tr[p[0]] = True
       assert en == set(tr), f'{mod}: key diff'
       print(f'{mod}: OK ({len(tr)} keys)')
   "
   ```
