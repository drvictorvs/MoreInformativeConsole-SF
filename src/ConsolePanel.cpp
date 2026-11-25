#include "ConsolePanel.h"

#include "AddressCheck.h"
#include "FormInfo.h"
#include "Highlight.h"
#include "Settings.h"

// user32 (linked in xmake.lua). Declared here because the shared Windows wrappers don't have it.
extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int a_key);

namespace MIC::ConsolePanel
{
	namespace
	{
		using Value = RE::Scaleform::GFx::Value;

		constexpr const char* kPanelName = "MIC_SF_InfoPanel";
		constexpr const char* kTextName = "MIC_SF_InfoText";
		double Padding() { return Settings::Get().padding; }

		struct State
		{
			std::uint64_t frame{ 0 };
			std::string   lastSelectionText;
			RE::TESFormID lastPrintedID{ 0 };
			std::string   lastHTML;
			std::uint32_t appliedStyle{ 0 };  // Settings::version the panel was styled with
			std::uint32_t highlightVersion{ 0 };  // Settings::version the selection highlight was set from
			bool          loggedNoParse{ false };
			bool          loggedNoBounds{ false };

			// Last layout: redone only when the text, the console's position or the settings change,
			// because setting the text field's width makes Scaleform re-lay out all of its text.
			bool          layoutValid{ false };
			double        layoutX{ 0.0 };
			double        layoutY{ 0.0 };
			double        layoutW{ 0.0 };
			double        layoutH{ 0.0 };
			double        layoutViewL{ 0.0 };
			double        layoutViewT{ 0.0 };
			double        layoutViewR{ 0.0 };

			// Scrolling: the panel is capped at the screen height and longer text scrolls.
			RE::TESFormID shownID{ 0 };        // reference whose info is in the panel
			bool          scrollable{ false };
			double        panelWidth{ 0.0 };   // background size, in the panel's own units
			double        panelHeight{ 0.0 };
			double        drawnScrollV{ 1.0 };  // scroll position the scroll bar was drawn for
			std::uint32_t keyFrames{ 0 };       // frames a scroll key has been held
			bool          hidden{ false };      // hidden with the show/hide key (until shown again)
			bool          toggleHeld{ false };
			std::uint32_t layoutVersion{ 0 };
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
			// With wheel scrolling on, the text field takes the mouse (and so blocks clicks under it).
			Set(a_panel, "mouseEnabled", Value(false));
			Set(a_panel, "mouseChildren", Value(settings.scrollWithMouse));
			Set(a_text, "mouseEnabled", Value(settings.scrollWithMouse));
			Set(a_text, "mouseWheelEnabled", Value(settings.scrollWithMouse));
			Set(a_text, "x", Value(settings.padding));
			Set(a_text, "y", Value(settings.padding));
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

		// The background, and a thin scroll bar on its right edge when the text scrolls.
		void DrawBackground(Value& a_panel, Value& a_text)
		{
			const auto& settings = Settings::Get();
			auto&       state = GetState();
			Value       graphics;
			if (!Member(a_panel, "graphics", graphics)) {
				return;
			}
			graphics.Invoke("clear");
			Value fill[2]{ Value(settings.backgroundColor & 0xFFFFFF), Value(settings.backgroundAlpha) };
			graphics.Invoke("beginFill", nullptr, fill, 2);
			Value rect[4]{ Value(0.0), Value(0.0), Value(state.panelWidth), Value(state.panelHeight) };
			graphics.Invoke("drawRect", nullptr, rect, 4);
			graphics.Invoke("endFill");

			Value        v;
			const double scrollV = Member(a_text, "scrollV", v) ? Num(v, 1.0) : 1.0;
			state.drawnScrollV = scrollV;
			if (!state.scrollable) {
				return;
			}
			const double maxScrollV = Member(a_text, "maxScrollV", v) ? Num(v, 1.0) : 1.0;
			const double bottomV = Member(a_text, "bottomScrollV", v) ? Num(v, scrollV) : scrollV;
			const double visible = std::max(1.0, bottomV - scrollV + 1.0);
			const double total = std::max(visible, maxScrollV + visible - 1.0);
			const double track = state.panelHeight - 2 * Padding();
			const double thumb = std::min(track, std::max(12.0, track * visible / total));
			const double at = maxScrollV > 1.0 ? (scrollV - 1.0) / (maxScrollV - 1.0) : 0.0;
			Value barFill[2]{ Value(settings.labelColor & 0xFFFFFF), Value(0.7) };
			graphics.Invoke("beginFill", nullptr, barFill, 2);
			Value bar[4]{ Value(state.panelWidth - 5.0), Value(Padding() + (track - thumb) * at), Value(3.0), Value(thumb) };
			graphics.Invoke("drawRect", nullptr, bar, 4);
			graphics.Invoke("endFill");
		}

		void Layout(Value& a_console, Value& a_panel, Value& a_text, bool a_textChanged)
		{
			const auto& settings = Settings::Get();
			auto&       state = GetState();

			// The console's own background tells us where the console is. It can be wider than the
			// screen, so the visible area is worked out too, in the console's coordinates.
			double bx = 0.0, by = 0.0, bw = 1280.0, bh = 0.0;
			double viewL = -1.0e9, viewT = -1.0e9, viewR = 1.0e9, viewB = 1.0e9;  // screen edges, if known
			Value  background;
			Value  bounds;
			if (Member(a_console, "Background", background) &&
				background.Invoke("getBounds", &bounds, &a_console, 1) && bounds.IsObject()) {
				Value v;
				if (Member(bounds, "x", v)) bx = Num(v, bx);
				if (Member(bounds, "y", v)) by = Num(v, by);
				if (Member(bounds, "width", v)) bw = Num(v, bw);
				if (Member(bounds, "height", v)) bh = Num(v, bh);
				// The same bounds measured on the stage give the console's offset and scale on screen.
				Value stage;
				Value sb;
				if (Member(a_console, "stage", stage) && stage.IsObject() &&
					background.Invoke("getBounds", &sb, &stage, 1) && sb.IsObject()) {
					double sx = 0.0, sy = 0.0, sw = 0.0;
					if (Member(sb, "x", v)) sx = Num(v, 0.0);
					if (Member(sb, "y", v)) sy = Num(v, 0.0);
					if (Member(sb, "width", v)) sw = Num(v, 0.0);
					const double k = sw > 0.0 ? bw / sw : 1.0;  // console units per stage unit
					viewL = bx - sx * k;
					viewT = by - sy * k;
					if (Member(stage, "stageWidth", v) && Num(v, 0.0) > 0.0) viewR = viewL + Num(v, 0.0) * k;
					if (Member(stage, "stageHeight", v) && Num(v, 0.0) > 0.0) viewB = viewT + Num(v, 0.0) * k;
				}
			} else if (!state.loggedNoBounds) {
				state.loggedNoBounds = true;
				REX::WARN("Could not read the console background bounds; placing the panel at the console origin");
			}

			if (!a_textChanged && state.layoutValid && state.layoutVersion == settings.version &&
				bx == state.layoutX && by == state.layoutY && bw == state.layoutW && bh == state.layoutH &&
				viewL == state.layoutViewL && viewT == state.layoutViewT && viewR == state.layoutViewR) {
				return;  // nothing that affects the layout changed
			}
			state.layoutValid = true;
			state.layoutVersion = settings.version;
			state.layoutX = bx;
			state.layoutY = by;
			state.layoutW = bw;
			state.layoutH = bh;
			state.layoutViewL = viewL;
			state.layoutViewT = viewT;
			state.layoutViewR = viewR;

			// The part of the console that is on screen, inside the margins.
			const double margin = settings.margin;
			const double left = std::max(bx, viewL) + margin;
			const double right = std::min(bx + bw, viewR) - margin;
			const double usable = std::max(120.0, right - left);

			// width/height are in the panel's own units; on screen they are multiplied by the scale.
			const double scale = settings.panelScale > 0.0 ? settings.panelScale : 1.0;
			const double screenWidth = std::clamp(usable * settings.widthPercent / 100.0, std::min(120.0, usable), usable);
			const double width = screenWidth / scale;
			Set(a_text, "width", Value(width - 2 * Padding()));
			Set(a_text, "autoSize", Value("left"));  // measure the whole text first

			Value        heightVal;
			const double fullText = Member(a_text, "height", heightVal) ? Num(heightVal, 0.0) : 0.0;

			// Over the console its bottom lines up with the console's (kept on screen); otherwise it sits above.
			const double bottom = settings.overConsole ? std::min(by + bh, viewB) : by;

			// Room for the panel: from the top of the screen down to its bottom, and at most the chosen
			// share of the screen height. Longer text gets a fixed-height field, which scrolls.
			double room = bottom - margin - (viewT + margin);
			if (viewT > -1.0e8 && viewB < 1.0e8) {
				room = std::min(room, (viewB - viewT) * settings.maxHeightPercent / 100.0 - 2 * margin);
			}
			const double maxText = room / scale - 2 * Padding();
			double       textHeight = fullText;
			state.scrollable = false;
			if (maxText > 40.0 && fullText > maxText) {
				Set(a_text, "autoSize", Value("none"));
				Set(a_text, "height", Value(maxText));
				textHeight = maxText;
				state.scrollable = true;
			}
			const double height = textHeight + 2 * Padding();
			const double screenHeight = height * scale;
			state.panelWidth = width;
			state.panelHeight = height;
			DrawBackground(a_panel, a_text);

			const double x = (settings.alignRight ? right - screenWidth : left) + settings.offsetX;
			double       y = bottom - margin - screenHeight + settings.offsetY;
			y = std::max(y, viewT + margin);  // never above the top of the screen
			Set(a_panel, "x", Value(x));
			Set(a_panel, "y", Value(y));

			if (settings.debugLog) {
				REX::INFO("Layout: background x={} y={} w={} h={}; screen {}..{} x {}..{}; panel x={} y={} w={} h={} scale={}",
					bx, by, bw, bh, viewL, viewR, viewT, viewB, x, y, screenWidth, screenHeight, scale);
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

			if (!settings.showPanel || state.hidden) {
				HideExistingPanel(console);  // off, or hidden with the show/hide key
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

			using Clock = std::chrono::steady_clock;
			const auto t0 = Clock::now();
			const auto sections = CollectReferenceInfo(ref);
			const auto t1 = Clock::now();
			const auto html = RenderHTML(sections);
			const bool changed = html != state.lastHTML;
			const bool sameRef = id == state.shownID;
			Value      v;
			const double keepScroll = sameRef && Member(text, "scrollV", v) ? Num(v, 1.0) : 1.0;
			state.shownID = id;
			if (changed) {
				Set(text, "htmlText", Value(html.c_str()));
				state.lastHTML = html;
			}
			const auto t2 = Clock::now();
			Layout(console, panel, text, changed);
			if (changed || !sameRef) {
				// New text resets the scroll position: keep it for the same reference, start at the top otherwise.
				const double maxScrollV = Member(text, "maxScrollV", v) ? Num(v, 1.0) : 1.0;
				Set(text, "scrollV", Value(std::clamp(keepScroll, 1.0, std::max(1.0, maxScrollV))));
			}
			if (state.scrollable && Member(text, "scrollV", v) && Num(v, 1.0) != state.drawnScrollV) {
				DrawBackground(panel, text);  // moved by the mouse wheel, or restored above
			}
			Set(panel, "visible", Value(true));
			const auto t3 = Clock::now();

			if (settings.debugLog) {
				using us = std::chrono::microseconds;
				const auto total = std::chrono::duration_cast<us>(t3 - t0).count();
				if (changed || total > 2000) {
					REX::INFO("Timing: collect {} us, text {} us{}, layout {} us; {}",
						std::chrono::duration_cast<us>(t1 - t0).count(),
						std::chrono::duration_cast<us>(t2 - t1).count(), changed ? " (changed)" : "",
						std::chrono::duration_cast<us>(t3 - t2).count(), LastCollectTimings());
				}
			}
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

	namespace
	{
		// Shows or hides the panel right away (the show/hide key), without waiting for a refresh.
		void SetPanelVisible(bool a_visible)
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return;
			}
			static const RE::BSFixedString consoleName{ "Console" };
			auto menu = ui->GetMenu(consoleName);
			if (!menu || !menu->uiMovie) {
				return;
			}
			Value console;
			Value panel;
			Value panelName(kPanelName);
			if (GetConsoleClip(*menu->uiMovie, *menu, console) &&
				console.Invoke("getChildByName", &panel, &panelName, 1) && panel.IsDisplayObject()) {
				Set(panel, "visible", Value(a_visible && GetState().shownID != 0));
			}
		}

		// Scrolls the panel by iScrollLines lines, or a page (a_direction -1 or 1).
		void ScrollPanel(int a_direction)
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return;
			}
			static const RE::BSFixedString consoleName{ "Console" };
			auto menu = ui->GetMenu(consoleName);
			if (!menu || !menu->uiMovie) {
				return;
			}
			Value console;
			if (!GetConsoleClip(*menu->uiMovie, *menu, console)) {
				return;
			}
			Value panel;
			Value text;
			Value panelName(kPanelName);
			Value textName(kTextName);
			if (!console.Invoke("getChildByName", &panel, &panelName, 1) || !panel.IsDisplayObject() ||
				!panel.Invoke("getChildByName", &text, &textName, 1) || !text.IsDisplayObject()) {
				return;
			}
			Value        v;
			const double scrollV = Member(text, "scrollV", v) ? Num(v, 1.0) : 1.0;
			const double bottomV = Member(text, "bottomScrollV", v) ? Num(v, scrollV) : scrollV;
			const double maxScrollV = Member(text, "maxScrollV", v) ? Num(v, 1.0) : 1.0;
			const auto   lines = Settings::Get().scrollLines;
			const double step = lines > 0 ? static_cast<double>(lines) : std::max(1.0, bottomV - scrollV);  // 0 = a page
			Set(text, "scrollV", Value(std::clamp(scrollV + a_direction * step, 1.0, std::max(1.0, maxScrollV))));
			DrawBackground(panel, text);
		}
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
		// The selection highlight applies with the console closed too, so it's set here, not below.
		if (state.highlightVersion != Settings::Get().version) {
			state.highlightVersion = Settings::Get().version;
			Highlight::Apply();
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
		const auto& settings = Settings::Get();

		// Show/hide key: acts on the press, not while held.
		if (settings.toggleKey != 0) {
			const bool held = (GetAsyncKeyState(static_cast<int>(settings.toggleKey)) & 0x8000) != 0;
			if (held && !state.toggleHeld) {
				state.hidden = !state.hidden;
				SetPanelVisible(!state.hidden);
			}
			state.toggleHeld = held;
		}

		// Scroll keys, checked every frame so scrolling feels immediate: one step on press, then
		// another every iScrollRepeatFrames frames while held.
		if (settings.scrollKeys && state.scrollable && !state.hidden) {
			const bool down = (GetAsyncKeyState(static_cast<int>(settings.scrollDownKey)) & 0x8000) != 0;
			const bool up = (GetAsyncKeyState(static_cast<int>(settings.scrollUpKey)) & 0x8000) != 0;
			const int  direction = down ? 1 : (up ? -1 : 0);
			if (direction == 0) {
				state.keyFrames = 0;
			} else if (state.keyFrames++ % std::max<std::uint32_t>(1, settings.scrollRepeatFrames) == 0) {
				ScrollPanel(direction);
			}
		}

		if (state.frame % settings.refreshFrames != 0) {
			return;
		}
		Update();
	}
}
