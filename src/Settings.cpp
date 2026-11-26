#include "Settings.h"

#include "vendor/OSFSettings.h"

namespace MIC
{
	namespace
	{
		enum class Kind
		{
			Bool,
			UInt,
			Double,
			Color,   // INI: 0xRRGGBB; OSF: string "#RRGGBB"
			String,
			Align,    // INI: bAlignRight 0/1 (older INI files)
			Position  // INI: sPosition right/left/custom; OSF: enum "right"/"left"/"custom"
		};

		struct Option
		{
			const char* iniSection;
			const char* iniKey;
			const char* osfKey;  // nullptr = INI only
			Kind        kind;
			void*       target;
			double      min{ 0.0 };
			double      max{ 0.0 };  // max <= min: no clamp
		};

		std::vector<Option> Options(Settings& s)
		{
			return {
				{ "General", "bShowPanel", "showPanel", Kind::Bool, &s.showPanel },
				{ "General", "bPrintToConsole", "printToConsole", Kind::Bool, &s.printToConsole },
				{ "General", "iRefreshFrames", "refreshFrames", Kind::UInt, &s.refreshFrames, 1, 240 },
				{ "General", "bDebugLog", "debugLog", Kind::Bool, &s.debugLog },

				{ "Panel", "bAlignRight", nullptr, Kind::Align, &s.position },
				{ "Panel", "sPosition", "alignment", Kind::Position, &s.position },
				{ "Panel", "fBoxLeft", "boxLeft", Kind::Double, &s.boxLeft, 0.0, 1920.0 },
				{ "Panel", "fBoxTop", "boxTop", Kind::Double, &s.boxTop, 0.0, 1080.0 },
				{ "Panel", "fBoxWidth", "boxWidth", Kind::Double, &s.boxWidth, 120.0, 1920.0 },
				{ "Panel", "fBoxHeight", "boxHeight", Kind::Double, &s.boxHeight, 60.0, 1080.0 },
				{ "Panel", "bOverConsole", "overConsole", Kind::Bool, &s.overConsole },
				{ "Panel", "fMaxHeightPercent", "maxHeightPercent", Kind::Double, &s.maxHeightPercent, 20.0, 100.0 },
				{ "Panel", "bScrollWithMouse", "scrollWithMouse", Kind::Bool, &s.scrollWithMouse },
				{ "Panel", "bScrollKeys", "scrollKeys", Kind::Bool, &s.scrollKeys },
				{ "Panel", "iScrollUpKey", nullptr, Kind::UInt, &s.scrollUpKey, 1, 254 },
				{ "Panel", "iScrollDownKey", nullptr, Kind::UInt, &s.scrollDownKey, 1, 254 },
				{ "Panel", "iScrollLines", "scrollLines", Kind::UInt, &s.scrollLines, 0, 50 },
				{ "Panel", "iScrollRepeatFrames", "scrollRepeatFrames", Kind::UInt, &s.scrollRepeatFrames, 1, 120 },
				{ "Panel", "iToggleKey", "toggleKey", Kind::UInt, &s.toggleKey, 0, 254 },
				{ "Panel", "fPadding", "padding", Kind::Double, &s.padding, 0.0, 60.0 },
				{ "Panel", "fWidthPercent", "widthPercent", Kind::Double, &s.widthPercent, 10, 100 },
				{ "Panel", "fScale", "panelScale", Kind::Double, &s.panelScale, 0.25, 4 },
				{ "Panel", "iFontSize", "fontSize", Kind::UInt, &s.fontSize, 0, 96 },
				{ "Panel", "fMargin", "margin", Kind::Double, &s.margin, 0.0, 200.0 },
				{ "Panel", "fOffsetX", "offsetX", Kind::Double, &s.offsetX },
				{ "Panel", "fOffsetY", "offsetY", Kind::Double, &s.offsetY },
				{ "Panel", "iBackgroundColor", "backgroundColor", Kind::Color, &s.backgroundColor },
				{ "Panel", "fBackgroundAlpha", "backgroundOpacity", Kind::Double, &s.backgroundAlpha, 0, 1 },
				{ "Panel", "iLabelColor", "labelColor", Kind::Color, &s.labelColor },
				{ "Panel", "iHeaderColor", "headerColor", Kind::Color, &s.headerColor },

				{ "Highlight", "bHighlightSelection", "highlightSelection", Kind::Bool, &s.highlightSelection },
				{ "Highlight", "iHighlightColor", "highlightColor", Kind::Color, &s.highlightColor },
				{ "Highlight", "fHighlightAlpha", "highlightOpacity", Kind::Double, &s.highlightAlpha, 0, 1 },
				{ "Highlight", "fHighlightOutline", "highlightOutline", Kind::Double, &s.highlightOutline, 0, 10 },

				{ "Content", "bShowReference", "showReference", Kind::Bool, &s.showReference },
				{ "Content", "bShowBase", "showBase", Kind::Bool, &s.showBase },
				{ "Content", "bShowModel", "showModel", Kind::Bool, &s.showModel },
				{ "Content", "bShowKeywords", "showKeywords", Kind::Bool, &s.showKeywords },
				{ "Content", "iMaxKeywords", "maxKeywords", Kind::UInt, &s.maxKeywords, 0, 200 },
				{ "Content", "bShowNPC", "showNPC", Kind::Bool, &s.showNPC },
				{ "Content", "bShowFactions", "showFactions", Kind::Bool, &s.showFactions },
				{ "Content", "bFactionsAsList", "factionsAsList", Kind::Bool, &s.factionsAsList },
				{ "Content", "iMaxFactions", "maxFactions", Kind::UInt, &s.maxFactions, 1, 200 },
				{ "Content", "bShowLock", "showLock", Kind::Bool, &s.showLock },
				{ "Content", "bShowPosition", "showPosition", Kind::Bool, &s.showPosition },
				{ "Content", "bShowLocationChain", "showLocationChain", Kind::Bool, &s.showLocationChain },
				{ "Content", "bShowInventory", "showInventory", Kind::Bool, &s.showInventory },
				{ "Content", "iMaxInventoryItems", "maxInventoryItems", Kind::UInt, &s.maxInventoryItems, 0, 200 },
				{ "Content", "bShowAI", "showAI", Kind::Bool, &s.showAI },
				{ "Content", "bShowAliases", "showAliases", Kind::Bool, &s.showAliases },
				{ "Content", "iMaxAliases", "maxAliases", Kind::UInt, &s.maxAliases, 1, 100 },
				{ "Content", "bShowWorn", "showWorn", Kind::Bool, &s.showWorn },
				{ "Content", "iMaxWornItems", "maxWornItems", Kind::UInt, &s.maxWornItems, 0, 64 },
				{ "Content", "bShowWornModels", "showWornModels", Kind::Bool, &s.showWornModels },

				{ "Details", "bShowActorState", "showActorState", Kind::Bool, &s.showActorState },
				{ "Details", "bShowMaterialSwaps", "showMaterialSwaps", Kind::Bool, &s.showMaterialSwaps },
				{ "Details", "iMaxSwapEntries", "maxSwapEntries", Kind::UInt, &s.maxSwapEntries, 0, 64 },
				{ "Details", "bShowActorValues", "showActorValues", Kind::Bool, &s.showActorValues },
				{ "Details", "bActorValuesAsList", "actorValuesAsList", Kind::Bool, &s.actorValuesAsList },
				{ "Details", "bAVHealth", "avHealth", Kind::Bool, &s.avHealth },
				{ "Details", "bAVOxygen", "avOxygen", Kind::Bool, &s.avOxygen },
				{ "Details", "bAVCarry", "avCarry", Kind::Bool, &s.avCarry },
				{ "Details", "bAVSpeed", "avSpeed", Kind::Bool, &s.avSpeed },
				{ "Details", "bAVAggression", "avAggression", Kind::Bool, &s.avAggression },
				{ "Details", "bAVConfidence", "avConfidence", Kind::Bool, &s.avConfidence },
				{ "Details", "bAVAssistance", "avAssistance", Kind::Bool, &s.avAssistance },
				{ "Details", "bAVMorality", "avMorality", Kind::Bool, &s.avMorality },
				{ "Details", "bAVHideSpacesuit", "avHideSpacesuit", Kind::Bool, &s.avHideSpacesuit },
				{ "Details", "bAVHideHelmet", "avHideHelmet", Kind::Bool, &s.avHideHelmet },
				{ "Details", "sExtraActorValues", "extraActorValues", Kind::String, &s.extraActorValues },
				{ "Details", "bShowMagicEffects", "showMagicEffects", Kind::Bool, &s.showMagicEffects },
				{ "Details", "iMaxMagicEffects", "maxMagicEffects", Kind::UInt, &s.maxMagicEffects, 1, 100 },
				{ "Details", "bShowBounds", "showBounds", Kind::Bool, &s.showBounds },
				{ "Details", "bShowGravity", "showGravity", Kind::Bool, &s.showGravity },
				{ "Details", "bShowGraphVariables", "showGraphVariables", Kind::Bool, &s.showGraphVariables },
				{ "Details", "sGraphVariables", "graphVariables", Kind::String, &s.graphVariables },
				{ "Details", "bShowEngineDetails", "showEngineDetails", Kind::Bool, &s.showEngineDetails },
			};
		}

		std::string Trim(std::string_view a_str)
		{
			const auto first = a_str.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_str.find_last_not_of(" \t\r\n");
			return std::string(a_str.substr(first, last - first + 1));
		}

		std::string Lower(std::string a_str)
		{
			std::ranges::transform(a_str, a_str.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_str;
		}

		std::optional<bool> ParseBool(const std::string& a_value)
		{
			const auto v = Lower(a_value);
			if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
			if (v == "0" || v == "false" || v == "no" || v == "off") return false;
			return std::nullopt;
		}

		std::optional<PanelPosition> ParsePosition(const std::string& a_value)
		{
			const auto v = Lower(Trim(a_value));
			if (v == "right") return PanelPosition::Right;
			if (v == "left") return PanelPosition::Left;
			if (v == "custom") return PanelPosition::Custom;
			return std::nullopt;
		}

		std::optional<double> ParseDouble(const std::string& a_value)
		{
			try {
				std::size_t used = 0;
				const auto  v = std::stod(a_value, &used);
				return used ? std::optional<double>(v) : std::nullopt;
			} catch (...) {
				return std::nullopt;
			}
		}

		// Accepts 0xRRGGBB, #RRGGBB or RRGGBB.
		std::optional<std::uint32_t> ParseColor(std::string a_value)
		{
			a_value = Trim(a_value);
			if (a_value.starts_with('#')) {
				a_value.erase(0, 1);
			} else if (a_value.starts_with("0x") || a_value.starts_with("0X")) {
				a_value.erase(0, 2);
			}
			if (a_value.empty() || a_value.size() > 6 || !std::ranges::all_of(a_value, [](unsigned char c) { return std::isxdigit(c); })) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(std::stoul(a_value, nullptr, 16));
		}

		double Clamp(const Option& a_opt, double a_value)
		{
			return a_opt.max > a_opt.min ? std::clamp(a_value, a_opt.min, a_opt.max) : a_value;
		}

		void SetFromText(const Option& a_opt, const std::string& a_value)
		{
			switch (a_opt.kind) {
			case Kind::Bool:
				if (const auto v = ParseBool(a_value)) *static_cast<bool*>(a_opt.target) = *v;
				break;
			case Kind::Align:
				if (const auto v = ParseBool(a_value)) *static_cast<PanelPosition*>(a_opt.target) = *v ? PanelPosition::Right : PanelPosition::Left;
				break;
			case Kind::Position:
				if (const auto v = ParsePosition(a_value)) *static_cast<PanelPosition*>(a_opt.target) = *v;
				break;
			case Kind::UInt:
				if (const auto v = ParseDouble(a_value)) *static_cast<std::uint32_t*>(a_opt.target) = static_cast<std::uint32_t>(std::max(0.0, Clamp(a_opt, *v)));
				break;
			case Kind::Double:
				if (const auto v = ParseDouble(a_value)) *static_cast<double*>(a_opt.target) = Clamp(a_opt, *v);
				break;
			case Kind::Color:
				if (const auto v = ParseColor(a_value)) *static_cast<std::uint32_t*>(a_opt.target) = *v & 0xFFFFFF;
				break;
			case Kind::String:
				*static_cast<std::string*>(a_opt.target) = a_value;
				break;
			}
		}

		void LoadINI(Settings& a_settings)
		{
			const auto    path = std::filesystem::path("Data/SFSE/Plugins/MoreInformativeConsoleSF.ini");
			std::ifstream file(path);
			if (!file) {
				REX::INFO("No {} found; using defaults", path.string());
				return;
			}

			const auto  options = Options(a_settings);
			std::string section;
			std::string line;
			while (std::getline(file, line)) {
				if (const auto comment = line.find_first_of(";#"); comment != std::string::npos) {
					line.erase(comment);
				}
				line = Trim(line);
				if (line.empty()) {
					continue;
				}
				if (line.front() == '[' && line.back() == ']') {
					section = Lower(Trim(std::string_view(line).substr(1, line.size() - 2)));
					continue;
				}
				const auto eq = line.find('=');
				if (eq == std::string::npos) {
					continue;
				}
				const auto key = Lower(Trim(std::string_view(line).substr(0, eq)));
				const auto value = Trim(std::string_view(line).substr(eq + 1));
				for (const auto& opt : options) {
					// [Experimental] is the old name of [Details]; INI files from 1.2 and earlier still load.
					const bool sameSection = Lower(opt.iniSection) == section || (section == "experimental" && Lower(opt.iniSection) == "details");
					if (sameSection && Lower(opt.iniKey) == key) {
						SetFromText(opt, value);
						break;
					}
				}
			}
		}

		// ---- OSF Settings -------------------------------------------------------------------------

		OSFSettings::API::Client& Client()
		{
			static OSFSettings::API::Client client;
			return client;
		}

		std::atomic<bool> g_osfChanged{ false };
		bool              g_osfAvailable{ false };

		void OnOSFChanged(const char*, const char*, void*) noexcept
		{
			// May run on any thread: only flag it; the game thread re-reads.
			g_osfChanged.store(true);
		}

		void LoadOSF(Settings& a_settings)
		{
			auto& client = Client();
			if (!g_osfAvailable || !client.IsReady()) {
				return;
			}
			using OSFSettings::API::Status;
			int applied = 0;
			for (const auto& opt : Options(a_settings)) {
				if (!opt.osfKey) {
					continue;
				}
				Status status = Status::UnknownSetting;
				switch (opt.kind) {
				case Kind::Bool:
					{
						bool v{};
						status = client.GetBool(OSF::kModID, opt.osfKey, &v);
						if (status == Status::Ok) *static_cast<bool*>(opt.target) = v;
						break;
					}
				case Kind::UInt:
					{
						std::int64_t v{};
						status = client.GetInt(OSF::kModID, opt.osfKey, &v);
						if (status == Status::Ok) *static_cast<std::uint32_t*>(opt.target) = static_cast<std::uint32_t>(std::max(0.0, Clamp(opt, static_cast<double>(v))));
						break;
					}
				case Kind::Double:
					{
						double v{};
						status = client.GetFloat(OSF::kModID, opt.osfKey, &v);
						if (status != Status::Ok) {  // offsets are ints in the schema
							std::int64_t i{};
							status = client.GetInt(OSF::kModID, opt.osfKey, &i);
							v = static_cast<double>(i);
						}
						if (status == Status::Ok) *static_cast<double*>(opt.target) = Clamp(opt, v);
						break;
					}
				case Kind::Color:
					{
						std::string v;
						status = client.GetString(OSF::kModID, opt.osfKey, v);
						if (status == Status::Ok) {
							if (const auto c = ParseColor(v)) *static_cast<std::uint32_t*>(opt.target) = *c & 0xFFFFFF;
						}
						break;
					}
				case Kind::String:
					{
						std::string v;
						status = client.GetString(OSF::kModID, opt.osfKey, v);
						if (status == Status::Ok) *static_cast<std::string*>(opt.target) = v;
						break;
					}
				case Kind::Align:
					break;  // INI only
				case Kind::Position:
					{
						std::string v;
						status = client.GetEnum(OSF::kModID, opt.osfKey, v);
						if (status == Status::Ok) {
							if (const auto p = ParsePosition(v)) *static_cast<PanelPosition*>(opt.target) = *p;
						}
						break;
					}
				}
				if (status == Status::Ok) {
					++applied;
				} else if (a_settings.debugLog) {
					REX::INFO("OSF Settings: could not read '{}' (status {})", opt.osfKey, std::to_underlying(status));
				}
			}
			REX::INFO("OSF Settings: applied {} values", applied);
		}
	}

	void Settings::Load()
	{
		const auto keepVersion = version;
		*this = Settings{};
		LoadINI(*this);
		LoadOSF(*this);
		version = keepVersion + 1;

		const char* where = position == PanelPosition::Custom ? "custom" : position == PanelPosition::Left ? "left" : "right";
		REX::INFO("Settings loaded (v{}): panel={} print={} position={} width={}% box={},{} {}x{} scale={} font={} source={}",
			version, showPanel, printToConsole, where, widthPercent, boxLeft, boxTop, boxWidth, boxHeight, panelScale, fontSize,
			g_osfAvailable ? "INI + OSF Settings" : "INI");
	}

	namespace OSF
	{
		void Init()
		{
			auto& client = Client();
			if (!client.Init()) {
				REX::INFO("OSF Settings not found; using the INI only");
				return;
			}
			OSFSettings::API::Subscription token{};
			const auto status = client.Subscribe(kModID, OnOSFChanged, nullptr, &token);
			if (status != OSFSettings::API::Status::Ok) {
				REX::WARN("OSF Settings: subscribing to '{}' failed (status {}); is the schema installed?", kModID, std::to_underlying(status));
				return;
			}
			g_osfAvailable = true;
			g_osfChanged.store(true);  // read once the game thread runs
			REX::INFO("OSF Settings connected (API {:08X})", client.Version());
		}

		bool ConsumeChanged()
		{
			return g_osfChanged.exchange(false);
		}
	}
}
