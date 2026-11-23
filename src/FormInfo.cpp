#include "FormInfo.h"

#include "Settings.h"

namespace MIC
{
	namespace
	{
		constexpr float kRadToDeg = 57.29577951308232f;

		std::string Hex(RE::TESFormID a_id) { return std::format("{:08X}", a_id); }

		std::string SafeStr(const char* a_str) { return a_str ? std::string(a_str) : std::string(); }

		std::string FileName(const RE::TESFile* a_file)
		{
			if (!a_file) {
				return {};
			}
			// fileName is a fixed char[260]; never read past it.
			const auto* begin = a_file->fileName;
			const auto* end = std::find(begin, begin + sizeof(a_file->fileName), '\0');
			return std::string(begin, end);
		}

		std::string EditorID(const RE::TESForm* a_form)
		{
			return a_form ? SafeStr(a_form->GetFormEditorID()) : std::string();
		}

		std::string Name(RE::TESForm* a_form)
		{
			if (!a_form) {
				return {};
			}
			if (auto* ref = a_form->AsReference()) {
				return SafeStr(ref->GetDisplayFullName());
			}
			if (auto* full = starfield_cast<RE::TESFullName*>(a_form)) {
				return SafeStr(full->GetFullName());
			}
			return {};
		}

		std::string TypeName(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return {};
			}
			const char* str = RE::FormTypeToString(a_form->GetFormType());
			return str ? std::string(str) : std::format("type {}", std::to_underlying(a_form->GetFormType()));
		}

		// "Name [EDID] (0001ABCD)", skipping the empty parts.
		std::string Describe(RE::TESForm* a_form)
		{
			if (!a_form) {
				return "none";
			}
			std::string out = Name(a_form);
			if (const auto edid = EditorID(a_form); !edid.empty()) {
				out += out.empty() ? edid : std::format(" [{}]", edid);
			}
			out += std::format("{}({})", out.empty() ? "" : " ", Hex(a_form->GetFormID()));
			return out;
		}

		std::string LastFileName(const RE::TESForm* a_form)
		{
			// The engine's "description owner" file: for plugin-defined forms this is the
			// file whose record is in effect (the last override). Unverified on every form type.
			return a_form ? FileName(a_form->GetDescriptionOwnerFile()) : std::string();
		}

		void AddFileLines(InfoSection& a_section, const RE::TESForm* a_form)
		{
			const auto defined = DefiningFileName(a_form->GetFormID());
			const auto last = LastFileName(a_form);
			a_section.lines.push_back({ "Defined in", defined.empty() ? "unknown" : defined });
			if (!last.empty() && last != defined) {
				a_section.lines.push_back({ "Last changed by", last });
			}
		}

		std::string Flags(const RE::TESForm* a_form)
		{
			std::vector<std::string_view> flags;
			if (a_form->IsPersistent()) flags.push_back("persistent");
			if (a_form->IsDisabled()) flags.push_back("disabled");
			if (a_form->IsDeleted()) flags.push_back("deleted");
			if (a_form->IsCreated()) flags.push_back("created in game");
			std::string out;
			for (const auto f : flags) {
				out += out.empty() ? std::string(f) : std::format(", {}", f);
			}
			return out;
		}

		std::string Point(const RE::NiPoint3& a_pt, float a_scale = 1.0f)
		{
			return std::format("{:.1f}, {:.1f}, {:.1f}", a_pt.x * a_scale, a_pt.y * a_scale, a_pt.z * a_scale);
		}

		std::string Model(RE::TESForm* a_form)
		{
			if (auto* model = starfield_cast<RE::TESModel*>(a_form)) {
				return SafeStr(model->model.c_str());
			}
			return {};
		}

		std::string EngineDetails(const RE::TESForm* a_form)
		{
			char buffer[512]{};
			a_form->GetFormDetailedString(buffer, sizeof(buffer));
			buffer[sizeof(buffer) - 1] = '\0';
			return buffer;
		}

		std::string EscapeHTML(std::string_view a_text)
		{
			std::string out;
			out.reserve(a_text.size());
			for (const char c : a_text) {
				switch (c) {
				case '&': out += "&amp;"; break;
				case '<': out += "&lt;"; break;
				case '>': out += "&gt;"; break;
				case '"': out += "&quot;"; break;
				default: out += c; break;
				}
			}
			return out;
		}
	}

	std::string DefiningFileName(RE::TESFormID a_formID)
	{
		const auto top = a_formID >> 24;
		if (top == 0xFF) {
			return "(created in save)";
		}

		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			return {};
		}
		const auto& files = handler->compiledFileCollection;

		if (top == 0xFE) {  // small master: FE xxx yyy
			const auto index = (a_formID >> 12) & 0xFFF;
			return index < files.smallFiles.size() ? FileName(files.smallFiles[index]) : std::string();
		}
		if (top == 0xFD) {  // medium master: FD xx yyyy
			const auto index = (a_formID >> 16) & 0xFF;
			return index < files.mediumFiles.size() ? FileName(files.mediumFiles[index]) : std::string();
		}
		return top < files.files.size() ? FileName(files.files[top]) : std::string();
	}

	std::vector<InfoSection> CollectReferenceInfo(RE::TESObjectREFR* a_ref)
	{
		const auto&              settings = Settings::Get();
		std::vector<InfoSection> sections;
		if (!a_ref) {
			return sections;
		}

		RE::TESBoundObject* base = a_ref->GetBaseObject().get();

		if (settings.showReference) {
			InfoSection ref{ "Reference", {} };
			ref.lines.push_back({ "Name", Name(a_ref).empty() ? "(no name)" : Name(a_ref) });
			ref.lines.push_back({ "Ref ID", std::format("{}  ({})", Hex(a_ref->GetFormID()), TypeName(a_ref)) });
			if (const auto edid = EditorID(a_ref); !edid.empty()) {
				ref.lines.push_back({ "Editor ID", edid });
			}
			AddFileLines(ref, a_ref);
			if (const auto flags = Flags(a_ref); !flags.empty()) {
				ref.lines.push_back({ "Flags", flags });
			}
			if (settings.showEngineDetails) {
				ref.lines.push_back({ "Engine", EngineDetails(a_ref) });
			}
			sections.push_back(std::move(ref));
		}

		if (settings.showBase && base) {
			InfoSection sec{ "Base object", {} };
			sec.lines.push_back({ "Name", Name(base).empty() ? "(no name)" : Name(base) });
			sec.lines.push_back({ "Base ID", std::format("{}  ({})", Hex(base->GetFormID()), TypeName(base)) });
			if (const auto edid = EditorID(base); !edid.empty()) {
				sec.lines.push_back({ "Editor ID", edid });
			}
			AddFileLines(sec, base);
			if (settings.showModel) {
				if (const auto model = Model(base); !model.empty()) {
					sec.lines.push_back({ "Model", model });
				}
			}
			if (settings.showEngineDetails) {
				sec.lines.push_back({ "Engine", EngineDetails(base) });
			}
			sections.push_back(std::move(sec));
		}

		if (settings.showActor) {
			if (auto* actor = a_ref->As<RE::Actor>()) {
				InfoSection sec{ "Actor", {} };
				if (auto* race = actor->race) {
					sec.lines.push_back({ "Race", Describe(race) });
					sec.lines.push_back({ "Race from", DefiningFileName(race->GetFormID()) });
				}
				if (auto* npc = actor->GetNPC()) {
					if (!base || npc->GetFormID() != base->GetFormID()) {
						sec.lines.push_back({ "NPC", Describe(npc) });
					}
				}
				if (!sec.lines.empty()) {
					sections.push_back(std::move(sec));
				}
			}
		}

		if (settings.showPosition) {
			InfoSection sec{ "Position", {} };
			sec.lines.push_back({ "Position", Point(a_ref->GetPosition()) });
			sec.lines.push_back({ "Rotation", Point(a_ref->GetAngle(), kRadToDeg) + " deg" });
			sec.lines.push_back({ "Scale", std::format("{:.2f}", a_ref->GetScale()) });
			if (auto* cell = a_ref->parentCell) {
				sec.lines.push_back({ "Cell", std::format("{}{}", Describe(cell), cell->IsInterior() ? "  interior" : "  exterior") });
				sec.lines.push_back({ "Cell from", DefiningFileName(cell->GetFormID()) });
			}
			if (auto* location = a_ref->GetCurrentLocation()) {
				sec.lines.push_back({ "Location", Describe(location) });
			}
			sections.push_back(std::move(sec));
		}

		return sections;
	}

	std::string RenderHTML(const std::vector<InfoSection>& a_sections)
	{
		const auto& settings = Settings::Get();
		std::string out;
		for (const auto& section : a_sections) {
			if (!out.empty()) {
				out += "<br>";
			}
			out += std::format("<font color='#{:06X}'><b>{}</b></font><br>", settings.headerColor & 0xFFFFFF, EscapeHTML(section.title));
			for (const auto& line : section.lines) {
				out += std::format("<font color='#{:06X}'>{}:</font> {}<br>", settings.labelColor & 0xFFFFFF,
					EscapeHTML(line.label), EscapeHTML(line.value));
			}
		}
		return out;
	}

	std::string RenderPlain(const std::vector<InfoSection>& a_sections)
	{
		std::string out;
		for (const auto& section : a_sections) {
			out += std::format("-- {} --\n", section.title);
			for (const auto& line : section.lines) {
				out += std::format("  {}: {}\n", line.label, line.value);
			}
		}
		return out;
	}
}
