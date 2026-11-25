#include "Highlight.h"

#include "AddressCheck.h"
#include "GameIDs.h"
#include "SafeMemory.h"
#include "Settings.h"

namespace MIC::Highlight
{
	namespace
	{
		constexpr std::string_view kSettingName = "aConsoleSelectedRefColor:Menu";

		std::uint32_t*               g_color = nullptr;  // the game setting's value, 0xAARRGGBB
		bool                         g_looked = false;
		std::optional<std::uint32_t> g_gameColor;        // the value before this mod first changed it

		std::uint32_t* GameColor()
		{
			if (g_looked) {
				return g_color;
			}
			g_looked = true;
			if (!Address::Get().selectionHighlight) {
				return nullptr;
			}
			// Make sure the ID still names this setting before writing to it.
			const auto  keyAddress = REL::Relocation<std::uintptr_t>{ GameID::aConsoleSelectedRefColor_Key }.address();
			const char* name = nullptr;
			if (!Safe::Read(reinterpret_cast<const void*>(keyAddress), name) || !name || Safe::CString(name, 64) != kSettingName) {
				REX::WARN("Selection highlight: the game's {} setting isn't where expected; highlight off", kSettingName);
				return nullptr;
			}
			auto* value = reinterpret_cast<std::uint32_t*>(REL::Relocation<std::uintptr_t>{ GameID::aConsoleSelectedRefColor }.address());
			if (!Safe::Readable(value, sizeof(std::uint32_t))) {
				return nullptr;
			}
			g_color = value;
			return g_color;
		}
	}

	void Apply()
	{
		auto* color = GameColor();
		if (!color) {
			return;
		}

		const auto&   settings = Settings::Get();
		std::uint32_t wanted = 0;
		if (settings.highlightSelection) {
			if (!g_gameColor) {
				g_gameColor = *color;
			}
			const auto alpha = static_cast<std::uint32_t>(std::lround(std::clamp(settings.highlightAlpha, 0.0, 1.0) * 255.0));
			wanted = (alpha << 24) | (settings.highlightColor & 0xFFFFFF);
		} else if (g_gameColor) {
			wanted = *g_gameColor;  // turned off during the session: put the game's value back
		} else {
			return;  // never changed: the game's (or StarfieldCustom.ini's) value stays
		}
		// The game removes the previous reference's highlight only while this color is non-zero, so
		// writing 0 would leave the last highlight on. A transparent non-zero color shows nothing.
		if (wanted == 0) {
			wanted = 1;
		}
		if (*color != wanted) {
			*color = wanted;
			REX::INFO("Selection highlight: {} set to {:08X} (AARRGGBB); shows on the next selected reference", kSettingName, wanted);
		}
	}
}
