# Reverse-engineering notes (Starfield 1.16.244.0)

Executable MD5 `0a73ecdc1d54b14a91818cddeb03aa9e`, image base `0x140000000`,
Address Library `versionlib-1-16-244-0.bin` (format 5, 1,274,968 IDs).

**Warning:** the commented IDs next to `{ 0 }` entries in the CommonLibSF fork's `IDs.h`
(for example `GetLock{ 0 }; // 107581`) are from an older numbering. In 1.16.244 they point to
other functions (`107581` -> `0x141c49850`, not the lock getter). Never reuse them as they stand.

## Method

1. Find a string the code of interest uses (console command output, setting name, Papyrus name).
2. Find the instructions that reference it (RIP-relative), and the function containing them (`.pdata`).
3. Disassemble; identify calls and globals; convert each address to its Address Library ID
   (`ID = offsets.index(address - 0x140000000)`).
4. Confirm independently: known IDs from the fork must appear where expected
   (here `PlayerCharacter::Singleton` 922868 and `ConsoleLog::Singleton` 938104 did).

`tools/sfre.py` and `tools/addrlib.py` do steps 1-3 from the raw executable, no Ghidra needed
(Python 3 with `pefile`, `capstone`, `numpy`).

## Locks

| ID | Address | What |
|---|---|---|
| 65051 | `140be4890` | `GetLockLevel` console command handler (prints `"GetLockLevel >> %0.f"`) |
| **64290** | `140ba0610` | `REFR_LOCK* GetLock(TESObjectREFR*)`: own `ExtraLock` (type 0x28, lock at +0x18); if none, the `ExtraTeleport` (0x29) linked reference's lock |
| **64342** | `140ba6740` | `uint16 GetAreaLevel(TESObjectREFR*, bool)`: encounter-zone level, else worldspace level map; console passes `false` |
| **45439** | `1405456d0` | `LOCK_LEVEL REFR_LOCK::GetLockLevel(const TESObjectREFR* owner) const` (lock in `rcx`, owner in `rdx`): computes the effective level as below, then tail-calls 45441 |
| 45441 | `1405457a0` | Level to `LOCK_LEVEL`: first of Easy/Average/Hard/VeryHard/Barred/Chained/Terminal/Inaccessible maxima with `level <= max` gives 0/1/2/3/7/8/6/5; otherwise 4 (Requires Key). Matches CommonLibSF's `LOCK_LEVEL` values |
| 37878 | `140347170` | Extra-data lookup by type (`ExtraDataList*`, type byte) |
| 113646 | `141eaa950` | Console print variant (next to the fork's `ConsoleLog::Print` 113647) |

When there is no owner (`rdx` = null), 45439 and the console use the player's level instead
(`PlayerCharacter::Singleton` 922868 -> `+0x98` -> `+0xE8`, ID 47237).

Layout confirmed: `TESObjectREFR::extraDataList` at 0xC8; `REFR_LOCK` flags at 0x0C (0x04 = leveled), base level at 0x0D.

Effective level (console rule): `level = base`; if leveled and `base != iLockLevelMaxImpossible`:
`level = min(99, base + GetAreaLevel(ref, false) * (int)fLeveledLockMult)`. The multiplier is
truncated to an integer (`vcvttss2si`), so the default 1.5 acts as 1.

### Lock-level settings (value IDs = setting object + 8)

| Setting | Value ID | Default | Name setting | Name value ID | Default name |
|---|---|---|---|---|---|
| iLockLevelMaxEasy | 887875 | 25 | sLockLevelNameEasy | 820329 | Easy |
| iLockLevelMaxAverage | 887878 | 50 | sLockLevelNameAverage | 820333 | Average |
| iLockLevelMaxHard | 887881 | 75 | sLockLevelNameHard | 820337 | Hard |
| iLockLevelMaxVeryHard | 887884 | 100 | sLockLevelNameVeryHard | 820341 | Very Hard |
| iLockLevelMaxBarred | 887887 | 251 | sLockLevelNameBarred | 820357 | Barred |
| iLockLevelMaxChained | 887890 | 252 | sLockLevelNameChained | 820361 | Chained |
| iLockLevelMaxTerminal | 887893 | 253 | sLockLevelNameTerminal | 820345 | Requires Terminal |
| iLockLevelMaxInaccessible | 887896 | 254 | sLockLevelNameinaccessible | 820349 | Inaccessible |
| iLockLevelMaxImpossible | 887899 | 255 | sLockLevelNameImpossible | 820353 | Requires Key |
| fLeveledLockMult | 887872 | 1.5 | | | |

Every rung is a range, read in the table's order: the first with `level <= max` wins, and anything above
Inaccessible is Requires Key (ID 45441). So a level of 180 counts as Barred. Integer and float
settings are registered at startup (`ID 36435` / `ID 36434`, value in `r8d` / `xmm2`); string
settings are static objects `{?, value char*, name char*}`. Data files can replace the values
(localized names, modded thresholds), so read them at run time.

Other settings seen: `fLockLevelBase` (obj `145ed9800`), `fLockLevelMult` (obj `145ed9820`, default -1.0),
`fLockLevelAutoattemptPoints{Novice,Average,Hard,VeryHard}`.

## Worn items, biped slots and armor addons

Confirmed offsets (constants in `src/GameIDs.h`, `GameID::Layout`). CommonLibSF's compiled layout agrees with all of them; only its offset comments were stale (written for a 0x48 `TESObject`; it is 0x50). The one real difference was the slot mask's width, fixed in the fork branch `fix/armor-biped-material-swaps`.

| Field | Offset | How confirmed |
|---|---|---|
| ARMO base classes | `TESFullName` 0xE8 ... `BGSBipedObjectForm` 0x200 ... `BGSInstanceNamingRulesForm` 0x270 | game RTTI base-class offsets |
| ARMO biped slot mask | 0x208, **u64** (CommonLibSF had u32) | the component loader reads the `BO64` subrecord as 8 bytes into the component at +8 |
| ARMO addon list `BSTArray<{u16 index; TESObjectARMA*}>` | 0x310 (size), 0x318 (data) | entries 0x10, addon at +8 |
| ARMA race (`TESRaceForm` at 0x50) | 0x58 | RTTI |
| ARMA biped slot mask (`BGSBipedObjectForm` at 0x60) | 0x68 | RTTI |
| ARMA male / female biped model (`BGSModelMaterialSwap`, 0x28 each) | 0x90 / 0xB8 | ARMA Load (ID 61147): the MOD2 branch passes `arma+0x90` and the MOD3 branch `arma+0xB8` to the model loaders (IDs 47641, 47645); later members (0x130, 0x158, 0x180, 0x1A0, 0x1E0-0x1F8) match too |
| ARMA additional races | 0x230 | follows from the confirmed layout |
| `TESModel::model` (BSFixedString) | 0x08 | setter ID 47641 writes `[this+8]` |

The worn flag is the inventory item's `flags & 7`, as in the fork. Slot numbers in the panel are bit indices (0-63).

## Material swaps

- `GetModelMaterialSwaps` (ID 47660, `0x1405F1F10`): `void (const TESModel* component, Visitor* visitor, bool create)`. Takes the global swap map's write lock, looks the component up (the key is the component's address), and if found (or `create`) calls `visitor->vtable[1](visitor, const Array*)`, then unlocks. Array is `{ u32 size; u32 capacity; BGSLayeredMaterialSwap** data }`.
- Callers: TESModel code (ID 47640) with `create = false`; the model loader (ID 47645) with `create = true` while reading swap subrecords. The mod calls it with `false` and its visitor only copies the pointers, because the lock is held during the call.
- `BGSLayeredMaterialSwap` (LMSW, form type 0xC6): entries `BSTArray` at +0x50, 0x1C0 bytes each: key material BSFixedString at +0x00, `KeyMaterialPathForm` at +0x08, `OverrideMaterialPath` BSFixedString at +0x10.
- Not done yet: the reference's own `ExtraMaterialSwap` (0xB5, XMSP) and the swaps the renderer actually applied to the loaded 3D.

## Editor IDs

Most form types in Starfield don't keep their editor ID on the form (`GetFormEditorID` returns an empty string for factions, packages, quests, material swaps and more). The engine still has them in a global hash map, which `TESForm::LookupByEditorID` (ID 47403) searches:

- **ID 883342**: pointer to the map.
- Map layout (from 47403 and its find routine, ID 35641): entries at +0x10, capacity at +0x18 (a power of two), `BSReadWriteLock` at +0x30.
- Entry, 0x18 bytes: `BSFixedString` key at +0x00, `TESForm*` at +0x08, `u32` next index at +0x10 (`0xFFFFFFFF` = empty slot; a chain ends at index == capacity).

The mod walks the map once, under its read lock, and keeps the editor IDs of factions, packages, quests and material swaps by form ID.

## Contributing to CommonLibSF

`libxse/commonlibsf` (which OSF's fork syncs from) still has both at 0 in `include/RE/IDs.h`:
`TESObjectREFR::GetLock{ 0 }; // 107581` -> **64290**, and `REFR_LOCK::GetLockLevel{ 0 }; // 84103` -> **45439**.
Both match the signatures CommonLibSF already declares (`REFR_LOCK* TESObjectREFR::GetLock() const`;
`LOCK_LEVEL REFR_LOCK::GetLockLevel(const TESObjectREFR*) const`).
