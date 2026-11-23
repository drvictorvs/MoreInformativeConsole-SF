# More Informative Console SF

A Starfield take on Skyrim's More Informative Console. Click a reference with the console open, and a panel above the console shows what it is and where it came from:

- **Reference:** name, ref ID and type, editor ID, the plugin that defines it, the plugin that last changed it, and flags (persistent, disabled, deleted, created in game).
- **Base object:** name, base ID and type, editor ID, defining and last-changing plugins, model path.
- **Actor:** race, and the plugin it comes from.
- **Position:** coordinates, rotation in degrees, scale, cell (interior or exterior, and its plugin), location.

Optionally, the same text can also be printed into the console history (`bPrintToConsole`).

Status: 0.1.1, not yet confirmed in game. 0.1.0 stopped the game with an Address Library error because it called `TESObjectREFR::GetScale`, whose ID is a 0 placeholder in this CommonLibSF fork. 0.1.1 reads the stored scale instead, and adds the safety check below.

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

The log lists each missing ID and ends with `Address check: panel=... consolePrint=... location=... pluginNames=...`.

**When adding code:** if a new call reaches an engine function, add its ID to the list in `AddressCheck.cpp` and guard the call with the matching flag. Search `include/RE/IDs.h` for `{ 0 }` first: those are known placeholders and must not be called at all.

## Requirements

- SFSE and Address Library for SFSE Plugins.
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
```

## Test

1. Start the game and load a save. In `Documents\My Games\Starfield\SFSE\Logs\MoreInformativeConsoleSF.log`, the `Address check:` line should show `true` for all four features, followed by `Console watcher registered`.
2. Open the console and click an NPC. The log should then say `Info panel added to the console`, and the panel should appear above the console.
3. Click a container, a door and a piece of clutter, then type `prid 14` to select yourself. The panel should follow each selection.

If nothing appears, set `bDebugLog = 1`, repeat step 2, and send me the log. Three things in it tell me where it failed:

- `Console selection text: "..."` shows exactly what the console displays. If it holds no 8-digit ID, the parser needs adjusting.
- `Could not create the info panel objects` means the console movie refused runtime objects.
- `Layout: ...` gives the panel's position. If the numbers look sensible but you can't see the panel, adjust `fOffsetX`/`fOffsetY`.

## Known risks

- **Thread.** SFSE runs its tasks on the game thread during command processing. I'm assuming the console movie is safe to touch there. If you get a crash when clicking a reference, this is the first suspect; the PDB lets Trainwreck point at it.
- **"Last changed by"** comes from the engine's description-owner file (`TESForm::GetDescriptionOwnerFile`). For plugin records this should be the winning override, but I haven't verified it for every form type. It's hidden when it matches "Defined in".
- **Bold text** is deliberately not used: the console's embedded font may lack a bold face, and missing faces render as nothing.
- **Fonts and size.** If the text is invisible but the dark box shows, the borrowed font isn't reaching the panel. Tell me and I'll switch to setting the font name directly.

## Not yet ported from Skyrim's MIC

Scrollable extra-info windows, inventory and quest pages, texture lists, and the hotkeys to cycle windows. Starfield's materials (`.mat` and the material database) also need their own approach before texture paths can be shown.

## License

GPL-3.0-or-later with CommonLibSF's modding exception (links against CommonLibSF).
