#pragma once

// Address Library IDs found for this mod (Starfield 1.16.244), not present (or 0) in the
// CommonLibSF fork. Each is checked by AddressCheck before use. See docs/RE-notes.md.
namespace MIC::GameID
{
	// REFR_LOCK* (TESObjectREFR*). The engine's lock getter: the reference's own ExtraLock (0x28), or
	// else the lock of the reference linked through ExtraTeleport (0x29), i.e. a load door's other side.
	// Called by the GetLockLevel console command (ID 65051).
	inline constexpr REL::ID GetLock{ 64290 };

	// std::uint16_t (TESObjectREFR*, bool). Level of the area the reference is in (encounter zone, or
	// the worldspace level map). The console passes false. Used for leveled locks.
	inline constexpr REL::ID GetAreaLevel{ 64342 };

	// LOCK_LEVEL REFR_LOCK::GetLockLevel(const TESObjectREFR* owner) const (CommonLibSF has it as 0).
	// Not called by this mod (it returns only the category); recorded for the CommonLibSF contribution.
	inline constexpr REL::ID REFR_LOCK_GetLockLevel{ 45439 };

	// Game settings: each ID is the setting's value (setting object + 8).
	inline constexpr REL::ID fLeveledLockMult{ 887872 };  // float, default 1.5

	struct LockRung
	{
		REL::ID maxValue;  // std::int32_t
		REL::ID name;      // const char*
	};

	// In the order the game reads its ladder (ID 45441): the first rung with level <= max wins;
	// above all of them the lock is "Requires Key". Index = LOCK_LEVEL 0-3, then 7, 8, 6, 5, 4.
	inline constexpr LockRung kLockLadder[] = {
		{ REL::ID{ 887875 }, REL::ID{ 820329 } },  // iLockLevelMaxEasy 25,          sLockLevelNameEasy
		{ REL::ID{ 887878 }, REL::ID{ 820333 } },  // iLockLevelMaxAverage 50,       sLockLevelNameAverage
		{ REL::ID{ 887881 }, REL::ID{ 820337 } },  // iLockLevelMaxHard 75,          sLockLevelNameHard
		{ REL::ID{ 887884 }, REL::ID{ 820341 } },  // iLockLevelMaxVeryHard 100,     sLockLevelNameVeryHard
		{ REL::ID{ 887887 }, REL::ID{ 820357 } },  // iLockLevelMaxBarred 251,       sLockLevelNameBarred
		{ REL::ID{ 887890 }, REL::ID{ 820361 } },  // iLockLevelMaxChained 252,      sLockLevelNameChained
		{ REL::ID{ 887893 }, REL::ID{ 820345 } },  // iLockLevelMaxTerminal 253,     sLockLevelNameTerminal
		{ REL::ID{ 887896 }, REL::ID{ 820349 } },  // iLockLevelMaxInaccessible 254, sLockLevelNameinaccessible
		{ REL::ID{ 887899 }, REL::ID{ 820353 } },  // iLockLevelMaxImpossible 255,   sLockLevelNameImpossible ("Requires Key")
	};
	inline constexpr std::size_t kLockImpossible = 8;

	// Material swaps. void (const TESModel* component, Visitor* visitor, bool create).
	// Looks the model component up in the engine's global swap map under a write lock and, if found
	// (or create is true), calls visitor->vtable[1](visitor, const Array* swaps). Array is
	// { u32 size; u32 capacity; void** data }: BGSLayeredMaterialSwap* items. Called with
	// create=false by TESModel code (ID 47640) and with true by the model loader (ID 47645).
	inline constexpr REL::ID GetModelMaterialSwaps{ 47660 };

	// Pointer to the engine's editor ID -> form hash map (what TESForm::LookupByEditorID, ID 47403,
	// searches). Most form types don't keep their editor ID on the form itself in Starfield, but the
	// map has them. Layout, from 47403 and the find routine 35641:
	//   +0x10 entries (0x18 bytes each: BSFixedString key, TESForm* value, u32 next; next == -1: empty)
	//   +0x18 capacity (power of two)   +0x30 BSReadWriteLock
	inline constexpr REL::ID EditorIDMap{ 883342 };

	// Layouts confirmed on 1.16.244. CommonLibSF's compiled layout agrees (its offset comments are
	// stale), except that its biped slot mask is 32-bit; the game's is 64-bit. Read only through SafeMemory.
	namespace Layout
	{
		inline constexpr std::size_t ARMO_BipedSlots = 0x208;      // u64 bit mask
		inline constexpr std::size_t ARMO_Addons = 0x310;          // BSTArray<{u16 index; TESObjectARMA*}>, 0x10 each
		inline constexpr std::size_t ARMA_Race = 0x58;             // TESRace* (TESRaceForm at 0x50)
		inline constexpr std::size_t ARMA_BipedSlots = 0x68;       // u64 bit mask
		inline constexpr std::size_t ARMA_BipedModel[2] = { 0x90, 0xB8 };  // BGSModelMaterialSwap, male/female
		inline constexpr std::size_t ARMA_AdditionalRaces = 0x230;  // BSTArray<TESRace*> 
		inline constexpr std::size_t TESModel_Path = 0x08;          // BSFixedString
		inline constexpr std::size_t LMSW_Entries = 0x50;          // BSTArray<Entry>
		inline constexpr std::size_t LMSW_EntrySize = 0x1C0;
		inline constexpr std::size_t LMSW_Entry_Key = 0x00;        // BSFixedString (material being replaced)
		inline constexpr std::size_t LMSW_Entry_Override = 0x10;   // BSFixedString (OverrideMaterialPath)
	}
}
