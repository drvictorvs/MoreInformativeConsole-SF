# More Informative Console SF

A Starfield take on Skyrim's More Informative Console. Click a reference with the console open, and a panel above the console shows what it is and where it came from.

| Section | Shows |
|---|---|
| Reference | Name, ref ID and type, editor ID, defining plugin, last plugin to change it, flags (persistent, disabled, deleted, created in game) |
| Base object | Name, base ID and type, editor ID, plugins, model path, keywords |
| NPC | Race and its plugin; essential, protected, unique, invulnerable, ghost, respawns, sex; level (or player-scaled range); leveled template; factions with ranks, and the live rank when it differs; dead, combat search, in water |
| AI & animation | Running AI package (and the normal one under a run-once override), package type and step, queued idle with its animation event, graph and file, furniture in use, the line being spoken, follower command, light level |
| Lock | Locked or not; the game's own category name and level (e.g. "Requires Key (255)", "Hard (60, leveled: base 40 + area level 20 x 1)"); locks shared from the linked door; key |
| Inventory | Kinds and total count, first N items with counts, equipped marked `[E]` |
| Position | Coordinates, rotation, scale, cell and its plugin, location, parent locations |
| Experimental (off by default) | Combat, invulnerable, talking, scene; health, oxygen, carry weight, speed and AI values; animation graph variables you name; object size; gravity; engine debug text |

Everything is configurable in the **OSF Settings** menu (under "More Informative Console") when OSF Settings is installed, or in `MoreInformativeConsoleSF.ini` otherwise: each section, width, position, **scale**, **font size**, colors and opacity. Menu changes apply right away, the next time the console panel refreshes.

Status: 1.1.0 in development, not yet confirmed in game. 1.0 (the 0.1.1 code) is confirmed in game and released on Nexus Mods. 0.1.0 stopped the game with an Address Library error because it called `TESObjectREFR::GetScale`, whose ID is a 0 placeholder in this CommonLibSF fork; 0.1.1 read the stored scale instead and added the safety check below. 1.1.0 adds the sections above, the OSF Settings menu, and a Lock section built on functions and settings found by reverse-engineering 1.16.244 (see `docs/RE-notes.md`).

## How it works (and how it differs from the Skyrim mod)

Skyrim's MIC ships a replacement `console.swf` (ActionScript 2) that calls functions registered by its SKSE plugin. Starfield's console is ActionScript 3, gets its data from the game through Bethesda's data-shuttle system, and SFSE has no way to register Scaleform functions. So this mod takes another route, and **does not replace `console.swf`**:

1. **Which reference is selected.** The game writes the selection into the console's `CurrentSelection` text field (through the SWF's `currentSelection` setter). The plugin reads that text and takes the form ID from it, e.g. `Sarah Morgan (00005791)`. This avoids hunting for an engine address that changes with each patch.
2. **The panel.** The plugin builds the panel at runtime inside the console movie: a Sprite with a TextField, added to `Menu_mc`. It borrows the console history's font, size and color, so it also matches console reskins. It sits just above the console's background, at the right edge by default.
3. **Timing.** An SFSE permanent task checks every frame whether the console is open. Only while it is open, and only every `iRefreshFrames` frames, does it read the selection and update the panel.

## Address Library safety

CommonLibSF looks up each engine function the first time it is called. When the ID is missing for the running game version (or is a `0` placeholder in the fork), it shows the "Failed to find offset for Address Library ID" box and closes the game. That can't be caught once it starts.

So at load, `AddressCheck.cpp` checks every ID the plugin can reach, directly or through CommonLibSF's inline code, against the loaded Address Library, without going through the lookup that aborts. It first makes the same checks the Address Library loader would (file present for this game version, supported format), since a failure there aborts too.

| Missing | Effect |
|---|---|
| Address Library file, or any panel ID (UI, form lookup, strings, RTTI cast, Scaleform) | The plugin loads but does nothing. |
| `ConsoleLog` | `bPrintToConsole` is ignored. |
| `TESObjectREFR::GetCurrentLocation` | No "Location" line. |
| `TESDataHandler` | "Defined in" shows `unknown`. |
| `BSReadWriteLock` read lock | No Inventory or Lock section. |
| `Actor::GetFactionRank` | Factions show base ranks only. |
| Handle lookup or `TESForm::DecRefCount` | No Furniture line. |
| `ActorValue::GetSingleton` | No actor values (experimental). |
| Lock IDs in `GameIDs.h` (lock getter, area level, lock-level settings) | Lock shows the raw level only, and only the reference's own lock. |

The log lists each missing ID and ends with an `Address check:` line giving each feature `true` or `false`.

## Reading data whose layout isn't confirmed

Most new sections read fields straight from game memory, using CommonLibSF's map of each record. Not every field position there has been confirmed on 1.16.244. A wrong number only shows an odd value, but following a wrong pointer would crash, so every pointer goes through `SafeMemory.cpp` first:

- The memory must be committed and readable (`VirtualQuery`).
- A form pointer is trusted only if the engine's form table maps its form ID back to the same address.
- Strings (idle names, spoken lines, model paths) are copied a page at a time, with each page checked first.

**Experimental options** are different: they call the game's own functions through its function tables. The Address Library check can't cover those, and a wrong slot in CommonLibSF's map crashes outright. That's why they're off by default. Turn them on one at a time; if one crashes the game, turn it off and send me the Trainwreck log. Editor position was left out for that reason (its function shares a name with two others, the most error-prone case).

**When adding code:** if a new call reaches an engine function, add its ID to the list in `AddressCheck.cpp` and guard the call with the matching flag. Search `include/RE/IDs.h` for `{ 0 }` first: those are known placeholders and must not be called at all.

## Reverse-engineering

`docs/RE-notes.md` records the functions and settings found for this mod, how they were found and how they were checked. `src/GameIDs.h` holds their Address Library IDs. `tools/sfre.py` (with `tools/addrlib.py`) finds string references, functions and IDs straight from `Starfield.exe`, without Ghidra.

## Requirements

- SFSE and Address Library for SFSE Plugins.
- Optional: OSF Settings 1.0 (for the in-game menu). Without it, the INI is used.
- To build: Visual Studio 2022 Build Tools (C++), XMake 3.0+ and Git.

## Build

In my setup, CommonLibSF (OSF's fork, commit `4242565`) lives next to the projects, and `xmake.lua` includes it as `../commonlibsf`.

```
...\Projects\commonlibsf
...\Projects\MoreInformativeConsole-SF   <- this folder
```

In this folder:

```powershell
xmake f -c -m releasedbg
xmake build -v
```

`MoreInformativeConsoleSF.dll` and `.pdb` land in `build\windows\x64\releasedbg\`. With `XSE_SF_MODS_PATH` (or `XSE_SF_GAME_PATH`) set, `xmake install` copies the DLL, PDB and INI into place. Otherwise, make a mod with:

```
Data/SFSE/Plugins/MoreInformativeConsoleSF.dll
Data/SFSE/Plugins/MoreInformativeConsoleSF.pdb
Data/SFSE/Plugins/MoreInformativeConsoleSF.ini
Data/SFSE/Plugins/OSF/Settings/schemas/moreinformativeconsole-sf.json
```

## Not yet ported from Skyrim's MIC

Scrollable extra-info windows, quest pages, texture lists, owner and enable parent (no layout in this CommonLibSF fork), and hotkeys to cycle windows (keys go to the console's text box while it's open). Starfield's materials (`.mat` and the material database) also need their own approach before texture paths can be shown.

## License

GPL-3.0-or-later with CommonLibSF's modding exception (links against CommonLibSF). `src/vendor/OSFSettings.h` is copied unchanged from OSF Settings (GPL-3.0 with its modding exception), commit `a7ffa18`, the release pairing OSF UI 2.0 names.
