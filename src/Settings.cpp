#include "Settings.h"

namespace MIC
{
	namespace
	{
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

		bool ParseBool(const std::string& a_value, bool a_default)
		{
			const auto v = Lower(a_value);
			if (v == "1" || v == "true" || v == "yes" || v == "on") {
				return true;
			}
			if (v == "0" || v == "false" || v == "no" || v == "off") {
				return false;
			}
			return a_default;
		}

		std::uint32_t ParseUInt(const std::string& a_value, std::uint32_t a_default)
		{
			try {
				std::size_t used = 0;
				const auto  v = std::stoul(a_value, &used, 0);  // accepts 0x... for colors
				return used ? static_cast<std::uint32_t>(v) : a_default;
			} catch (...) {
				return a_default;
			}
		}

		double ParseDouble(const std::string& a_value, double a_default)
		{
			try {
				std::size_t used = 0;
				const auto  v = std::stod(a_value, &used);
				return used ? v : a_default;
			} catch (...) {
				return a_default;
			}
		}
	}

	void Settings::Load()
	{
		const auto path = std::filesystem::path("Data/SFSE/Plugins/MoreInformativeConsoleSF.ini");
		std::ifstream file(path);
		if (!file) {
			REX::INFO("No {} found; using defaults", path.string());
			return;
		}

		std::string section;
		std::string line;
		while (std::getline(file, line)) {
			const auto comment = line.find_first_of(";#");
			if (comment != std::string::npos) {
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

			if (section == "general") {
				if (key == "bshowpanel") showPanel = ParseBool(value, showPanel);
				else if (key == "bprinttoconsole") printToConsole = ParseBool(value, printToConsole);
				else if (key == "irefreshframes") refreshFrames = std::max<std::uint32_t>(1, ParseUInt(value, refreshFrames));
				else if (key == "bdebuglog") debugLog = ParseBool(value, debugLog);
			} else if (section == "content") {
				if (key == "bshowreference") showReference = ParseBool(value, showReference);
				else if (key == "bshowbase") showBase = ParseBool(value, showBase);
				else if (key == "bshowactor") showActor = ParseBool(value, showActor);
				else if (key == "bshowposition") showPosition = ParseBool(value, showPosition);
				else if (key == "bshowmodel") showModel = ParseBool(value, showModel);
				else if (key == "bshowenginedetails") showEngineDetails = ParseBool(value, showEngineDetails);
			} else if (section == "panel") {
				if (key == "balignright") alignRight = ParseBool(value, alignRight);
				else if (key == "fwidthpercent") widthPercent = std::clamp(ParseDouble(value, widthPercent), 10.0, 100.0);
				else if (key == "fmargin") margin = ParseDouble(value, margin);
				else if (key == "foffsetx") offsetX = ParseDouble(value, offsetX);
				else if (key == "foffsety") offsetY = ParseDouble(value, offsetY);
				else if (key == "ifontsize") fontSize = ParseUInt(value, fontSize);
				else if (key == "ibackgroundcolor") backgroundColor = ParseUInt(value, backgroundColor);
				else if (key == "fbackgroundalpha") backgroundAlpha = std::clamp(ParseDouble(value, backgroundAlpha), 0.0, 1.0);
				else if (key == "ilabelcolor") labelColor = ParseUInt(value, labelColor);
				else if (key == "iheadercolor") headerColor = ParseUInt(value, headerColor);
			}
		}

		REX::INFO("Settings loaded: panel={} print={} refresh={} align={} width={}% font={}",
			showPanel, printToConsole, refreshFrames, alignRight ? "right" : "left", widthPercent, fontSize);
	}
}
