#pragma once

namespace MIC
{
	struct Settings
	{
		// [General]
		bool          showPanel{ true };        // draw the info panel above the console
		bool          printToConsole{ false };  // also print the info into the console history on each new selection
		std::uint32_t refreshFrames{ 15 };      // how often (in frames) the panel re-reads the selection while the console is open
		bool          debugLog{ false };        // log the raw selection text and layout numbers

		// [Content]
		bool showReference{ true };
		bool showBase{ true };
		bool showActor{ true };
		bool showPosition{ true };
		bool showModel{ true };
		bool showEngineDetails{ false };  // TESForm::GetFormDetailedString (engine debug text)

		// [Panel]
		bool          alignRight{ true };
		double        widthPercent{ 40.0 };  // of the console width
		double        margin{ 8.0 };
		double        offsetX{ 0.0 };
		double        offsetY{ 0.0 };
		std::uint32_t fontSize{ 0 };          // 0 = console's own size
		std::uint32_t backgroundColor{ 0x000000 };
		double        backgroundAlpha{ 0.65 };
		std::uint32_t labelColor{ 0x9FB4C8 };
		std::uint32_t headerColor{ 0xF2C14E };

		static Settings& Get()
		{
			static Settings s;
			return s;
		}

		void Load();
	};
}
