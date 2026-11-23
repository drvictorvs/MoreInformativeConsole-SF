#pragma once

// Address Library safety net.
//
// CommonLibSF resolves an engine function the first time it is called. If its Address Library ID is
// missing for the running game version (or is a 0 placeholder in the fork), REL::IDDB::offset() calls
// REX::FAIL, which shows a message box and ends the game. Nothing can catch that.
//
// So every engine function this plugin reaches, directly or through CommonLibSF inline code, is
// listed in AddressCheck.cpp and checked once at load against the loaded database, without going
// through offset(). Features whose IDs are missing are switched off and logged; the game carries on.
//
// Rule for new code: any call into CommonLibSF that resolves an ID must have that ID added to the
// list in AddressCheck.cpp and be guarded by the matching flag below.

namespace MIC::Address
{
	struct Features
	{
		bool core{ false };          // UI, form lookup, strings, RTTI casts, Scaleform: the panel itself
		bool consolePrint{ false };  // ConsoleLog: bPrintToConsole
		bool location{ false };      // TESObjectREFR::GetCurrentLocation: the "Location" line
		bool dataHandler{ false };   // TESDataHandler: "Defined in" plugin names
	};

	// Runs the check the first time; afterwards returns the cached result.
	const Features& Get();

	// True when the ID is non-zero and has an offset in the loaded Address Library.
	// Never calls REX::FAIL.
	bool IsPresent(std::uint64_t a_id);
}
