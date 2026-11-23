#pragma once

namespace MIC
{
	struct InfoLine
	{
		std::string label;
		std::string value;
	};

	struct InfoSection
	{
		std::string           title;
		std::vector<InfoLine> lines;
	};

	// Reads everything the panel shows about a reference. Runs on the game thread.
	std::vector<InfoSection> CollectReferenceInfo(RE::TESObjectREFR* a_ref);

	// "Starfield.esm", "MyMod.esm", "(created in save)", or "" if unknown.
	std::string DefiningFileName(RE::TESFormID a_formID);

	std::string RenderHTML(const std::vector<InfoSection>& a_sections);
	std::string RenderPlain(const std::vector<InfoSection>& a_sections);
}
