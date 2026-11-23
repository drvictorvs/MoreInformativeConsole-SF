#pragma once

namespace MIC::ConsolePanel
{
	// Called every frame from the SFSE task queue (game thread).
	void OnFrame();

	// Exposed for unit-style checks: pulls the form ID out of the console's
	// selection line, e.g. "Sarah Morgan (00005791)". Returns 0 when none is found.
	RE::TESFormID ParseSelection(std::string_view a_text);
}
