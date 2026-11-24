#include "ConsolePanel.h"

#include "AddressCheck.h"
#include "FormInfo.h"
#include "Settings.h"

namespace MIC::ConsolePanel
{
	namespace
	{
		using Value = RE::Scaleform::GFx::Value;

		constexpr const char* kPanelName = "MIC_SF_InfoPanel";
		constexpr const char* kTextName = "MIC_SF_InfoText";
		constexpr double      kPadding = 6.0;

		struct State
		{
			std::uint64_t frame{ 0 };
			std::string   lastSelectionText;
			RE::TESFormID lastPrintedID{ 0 };
			std::string   lastHTML;
			std::uint32_t appliedStyle{ 0 };  // Settings::version the panel was styled with
			bool          loggedNoParse{ false };
			bool          loggedNoBounds{ false };
		};

		State& GetState()
		{
			static State state;
			return state;
		}

		double Num(const Value& a_value, double a_default = 0.0)
		{
			if (a_value.IsNumber()) return a_value.GetNumber();
			if (a_value.IsInt()) return a_value.GetInt();
			if (a_value.IsUInt()) return a_value.GetUInt();
			return a_default;
		}

		bool Member(const Value& a_obj, const char* a_name, Value& a_out)
		{
			return a_obj.IsObject() && a_obj.GetMember(a_name, &a_out);
		}

		void Set(Value& a_obj, const char* a_name, const Value& a_value)
		{
			if (a_obj.IsObject()) {
				a_obj.SetMember(a_name, a_value);
			}
		}

		bool IsHexDigit(char c)
		{
			return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
		}

		// The console's root display object (Console class, instance "Menu_mc").
		bool GetConsoleClip(RE::Scaleform::GFx::Movie& a_movie, RE::IMenu& a_menu, Value& a_out)
		{
			if (a_movie.asMovieRoot) {
				Value root;
				if (a_movie.asMovieRoot->GetVariable(&root, "root") && Member(root, "Menu_mc", a_out) && a_out.IsDisplayObject()) {
					return true;
				}
			}
			if (a_menu.menuObj.IsDisplayObject()) {
				a_out = a_menu.menuObj;
				return true;
			}
			return false;
		}

		std::string ReadSelectionText(const Value& a_console)
		{
			Value field;
			Value text;
			if (Member(a_console, "CurrentSelection", field) && Member(field, "text", text) && text.IsString()) {
				return text.GetString() ? text.GetString() : "";
			}
			return {};
		}

		// Finds our panel, creating it (a Sprite with a TextField child) the first time,
		// and again whenever the game rebuilds the console movie.
		bool GetOrCreatePanel(RE::Scaleform::GFx::Movie& a_movie, Value& a_console, Value& a_panel, Value& a_text, bool& a_created)
		{
			a_created = false;
			Value nameArg(kPanelName);
			if (a_console.Invoke("getChildByName", &a_panel, &nameArg, 1) && a_panel.IsDisplayObject()) {
				Value textName(kTextName);
				return a_panel.Invoke("getChildByName", &a_text, &textName, 1) && a_text.IsDisplayObject();
			}

			if (!a_movie.asMovieRoot) {
				return false;
			}
			auto& root = *a_movie.asMovieRoot;
			root.CreateObject(&a_panel, "flash.display.Sprite");
			root.CreateObject(&a_text, "flash.text.TextField");
			if (!a_panel.IsDisplayObject() || !a_text.IsDisplayObject()) {
				REX::WARN("Could not create the info panel objects in the console movie");
				return false;
			}

			Set(a_panel, "name", Value(kPanelName));
			Set(a_panel, "mouseEnabled", Value(false));
			Set(a_panel, "mouseChildren", Value(false));

			Set(a_text, "name", Value(kTextName));
			Set(a_text, "x", Value(kPadding));
			Set(a_text, "y", Value(kPadding));

			Set(a_text, "multiline", Value(true));
			Set(a_text, "wordWrap", Value(true));
			Set(a_text, "selectable", Value(false));
			Set(a_text, "mouseEnabled", Value(false));
			Set(a_text, "autoSize", Value("left"));

			a_panel.Invoke("addChild", nullptr, &a_text, 1);
			a_console.Invoke("addChild", nullptr, &a_panel, 1);

			a_created = true;
			REX::INFO("Info panel added to the console");
			return true;
		}

		// Font and size. Borrows the console history's font so the panel matches the console (and
		// any console reskin), including whether that font is embedded. Re-run when settings change.
		void ApplyStyle(Value& a_console, Value& a_panel, Value& a_text)
		{
			const auto& settings = Settings::Get();
			Value       history;
			if (Member(a_console, "CommandHistory", history)) {
				Value format;
				if (Member(history, "defaultTextFormat", format) && format.IsObject()) {
					if (settings.fontSize > 0) {
						Set(format, "size", Value(static_cast<double>(settings.fontSize)));
					}
					Set(a_text, "defaultTextFormat", format);
				}
				Value embed;
				if (Member(history, "embedFonts", embed) && embed.IsBoolean()) {
					Set(a_text, "embedFonts", embed);
				}
				Value color;
				if (Member(history, "textColor", color)) {
					Set(a_text, "textColor", color);
				}
			}
			const double scale = settings.panelScale > 0.0 ? settings.panelScale : 1.0;
			Set(a_panel, "scaleX", Value(scale));
			Set(a_panel, "scaleY", Value(scale));
		}

		void HideExistingPanel(Value& a_console)
		{
			Value nameArg(kPanelName);
			Value panel;
			if (a_console.Invoke("getChildByName", &panel, &nameArg, 1) && panel.IsDisplayObject()) {
				Set(panel, "visible", Value(false));
			}
		}

		void Layout(Value& a_console, Value& a_panel, Value& a_text)
		{
			const auto& settings = Settings::Get();
			auto&       state = GetState();

			// The console's own background tells us where the console is on screen.
			double bx = 0.0, by = 0.0, bw = 1280.0;
			Value  background;
			Value  bounds;
			if (Member(a_console, "Background", background) &&
				background.Invoke("getBounds", &bounds, &a_console, 1) && bounds.IsObject()) {
				Value v;
				if (Member(bounds, "x", v)) bx = Num(v, bx);
				if (Member(bounds, "y", v)) by = Num(v, by);
				if (Member(bounds, "width", v)) bw = Num(v, bw);
			} else if (!state.loggedNoBounds) {
				state.loggedNoBounds = true;
				REX::WARN("Could not read the console background bounds; placing the panel at the console origin");
			}

			// width/height are in the panel's own units; on screen they are multiplied by the scale.
			const double scale = settings.panelScale > 0.0 ? settings.panelScale : 1.0;
			const double screenWidth = std::max(120.0, bw * settings.widthPercent / 100.0);
			const double width = screenWidth / scale;
			Set(a_text, "width", Value(width - 2 * kPadding));

			Value heightVal;
			const double textHeight = Member(a_text, "height", heightVal) ? Num(heightVal, 0.0) : 0.0;
			const double height = textHeight + 2 * kPadding;
			const double screenHeight = height * scale;

			Value graphics;
			if (Member(a_panel, "graphics", graphics)) {
				graphics.Invoke("clear");
				Value fill[2]{ Value(settings.backgroundColor & 0xFFFFFF), Value(settings.backgroundAlpha) };
				graphics.Invoke("beginFill", nullptr, fill, 2);
				Value rect[4]{ Value(0.0), Value(0.0), Value(width), Value(height) };
				graphics.Invoke("drawRect", nullptr, rect, 4);
				graphics.Invoke("endFill");
			}

			const double x = (settings.alignRight ? bx + bw - screenWidth - settings.margin : bx + settings.margin) + settings.offsetX;
			const double y = by - screenHeight - settings.margin + settings.offsetY;
			Set(a_panel, "x", Value(x));
			Set(a_panel, "y", Value(y));

			if (settings.debugLog) {
				REX::INFO("Layout: background x={} y={} w={}; panel x={} y={} w={} h={} scale={}", bx, by, bw, x, y, screenWidth, screenHeight, scale);
			}
		}

		void PrintToConsole(RE::TESFormID a_id, RE::TESObjectREFR* a_ref)
		{
			auto& state = GetState();
			if (a_id == state.lastPrintedID) {
				return;
			}
			state.lastPrintedID = a_id;
			if (!a_ref) {
				return;
			}
			if (auto* log = RE::ConsoleLog::GetSingleton()) {
				const auto text = RenderPlain(CollectReferenceInfo(a_ref));
				if (!text.empty()) {
					log->AddString(text.c_str());
				}
			}
		}

		void Update()
		{
			const auto& settings = Settings::Get();
			auto&       state = GetState();

			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return;
			}
			static const RE::BSFixedString consoleName{ "Console" };
			auto menu = ui->GetMenu(consoleName);
			if (!menu || !menu->uiMovie) {
				return;
			}
			auto& movie = *menu->uiMovie;

			Value console;
			if (!GetConsoleClip(movie, *menu, console)) {
				return;
			}

			const auto selection = ReadSelectionText(console);
			if (settings.debugLog && selection != state.lastSelectionText) {
				REX::INFO("Console selection text: \"{}\"", selection);
			}
			state.lastSelectionText = selection;

			const auto id = ParseSelection(selection);
			if (id == 0 && !selection.empty() && !state.loggedNoParse) {
				state.loggedNoParse = true;
				REX::WARN("Could not find a form ID in the console selection text \"{}\"", selection);
			}

			RE::TESObjectREFR* ref = nullptr;
			if (id != 0) {
				if (auto* form = RE::TESForm::LookupByID(id)) {
					ref = form->AsReference();
				}
			}

			if (settings.printToConsole && Address::Get().consolePrint) {
				PrintToConsole(id, ref);
			}

			if (!settings.showPanel) {
				HideExistingPanel(console);
				return;
			}

			Value panel;
			Value text;
			bool  created = false;
			if (!GetOrCreatePanel(movie, console, panel, text, created)) {
				return;
			}
			if (created || state.appliedStyle != settings.version) {
				ApplyStyle(console, panel, text);
				state.appliedStyle = settings.version;
				state.lastHTML.clear();  // re-set the text so the new format applies
			}

			if (!ref) {
				Set(panel, "visible", Value(false));
				return;
			}

			const auto html = RenderHTML(CollectReferenceInfo(ref));
			if (html != state.lastHTML) {
				Set(text, "htmlText", Value(html.c_str()));
				state.lastHTML = html;
			}
			Layout(console, panel, text);
			Set(panel, "visible", Value(true));
		}
	}

	RE::TESFormID ParseSelection(std::string_view a_text)
	{
		// Preferred: the last "(hex)" group, as in "Sarah Morgan (00005791)".
		for (auto close = a_text.rfind(')'); close != std::string_view::npos && close > 0; close = a_text.rfind(')', close - 1)) {
			const auto open = a_text.rfind('(', close);
			if (open == std::string_view::npos) {
				break;
			}
			const auto inner = a_text.substr(open + 1, close - open - 1);
			if (inner.size() == 8 && std::ranges::all_of(inner, IsHexDigit)) {
				return static_cast<RE::TESFormID>(std::stoul(std::string(inner), nullptr, 16));
			}
			if (open == 0) {
				break;
			}
		}

		// Fallback: the last standalone run of exactly 8 hex digits.
		for (std::size_t end = a_text.size(); end >= 8; --end) {
			const auto start = end - 8;
			const auto run = a_text.substr(start, 8);
			const bool leftOk = start == 0 || !std::isalnum(static_cast<unsigned char>(a_text[start - 1]));
			const bool rightOk = end == a_text.size() || !std::isalnum(static_cast<unsigned char>(a_text[end]));
			if (leftOk && rightOk && std::ranges::all_of(run, IsHexDigit)) {
				return static_cast<RE::TESFormID>(std::stoul(std::string(run), nullptr, 16));
			}
		}
		return 0;
	}

	void OnFrame()
	{
		if (!Address::Get().core) {
			return;  // never registered in that case; this is a second guard
		}
		auto& state = GetState();
		++state.frame;

		// OSF Settings changes arrive on another thread; re-read them here, on the game thread.
		if (OSF::ConsumeChanged()) {
			Settings::Get().Load();
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return;
		}
		static const RE::BSFixedString consoleName{ "Console" };
		if (!ui->IsMenuOpen(consoleName)) {
			state.lastPrintedID = 0;  // print again for the same reference after reopening
			return;
		}
		if (state.frame % Settings::Get().refreshFrames != 0) {
			return;
		}
		Update();
	}
}
