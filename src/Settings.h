#pragma once

namespace MIC
{
	// All options. Values come from MoreInformativeConsoleSF.ini; when OSF Settings is installed,
	// its menu values (Documents\My Games\Starfield\OSF\Settings\moreinformativeconsole-sf.json)
	// override the INI. Read and written on the game thread only.
	struct Settings
	{
		// General
		bool          showPanel{ true };
		bool          printToConsole{ false };
		std::uint32_t refreshFrames{ 15 };
		bool          debugLog{ false };

		// Panel
		bool          alignRight{ true };
		bool          overConsole{ false };  // bottom-align with the console instead of sitting above it
		double        widthPercent{ 40.0 };
		double        panelScale{ 1.0 };
		std::uint32_t fontSize{ 0 };  // 0 = console's own size
		double        margin{ 8.0 };
		double        offsetX{ 0.0 };
		double        offsetY{ 0.0 };
		std::uint32_t backgroundColor{ 0x000000 };
		double        backgroundAlpha{ 0.65 };
		std::uint32_t labelColor{ 0x9FB4C8 };
		std::uint32_t headerColor{ 0xF2C14E };

		// Sections
		bool          showReference{ true };
		bool          showBase{ true };
		bool          showModel{ true };
		bool          showKeywords{ true };
		std::uint32_t maxKeywords{ 12 };
		bool          showNPC{ true };
		bool          showFactions{ true };
		bool          showLock{ true };
		bool          showPosition{ true };
		bool          showLocationChain{ true };
		bool          showInventory{ true };
		std::uint32_t maxInventoryItems{ 8 };
		bool          showAI{ true };
		bool          showWorn{ true };
		std::uint32_t maxWornItems{ 16 };
		bool          showWornModels{ true };

		// Experimental: calls through the game's function tables; confirm in game before relying on them
		bool        showActorState{ false };
		bool        showMaterialSwaps{ false };
		std::uint32_t maxSwapEntries{ 6 };
		bool        showActorValues{ false };
		bool        showBounds{ false };
		bool        showGravity{ false };
		bool        showGraphVariables{ false };
		std::string graphVariables{ "iState, bIsSitting, Speed, Direction, bInJumpState" };
		bool        showEngineDetails{ false };

		// Incremented on every (re)load, so the panel knows to restyle.
		std::uint32_t version{ 0 };

		static Settings& Get()
		{
			static Settings s;
			return s;
		}

		// INI first, then OSF Settings values on top (if OSF Settings is available).
		void Load();
	};

	namespace OSF
	{
		inline constexpr const char* kModID = "moreinformativeconsole-sf";

		// SFSE kPostLoad: connect to OSF Settings and subscribe to changes. Safe if it isn't installed.
		void Init();

		// True once after OSF Settings reported a change; the game thread then calls Settings::Load().
		bool ConsumeChanged();
	}
}
