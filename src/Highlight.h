#pragma once

// Highlights the reference selected in the console with the engine's object highlight: the color
// fill and outline that the hand scanner, outpost building and the crafting menus draw. It covers the
// whole object whatever its materials, and is shown only while the console is open.
//
// The mod owns one of the game's HighlightManager objects with its own style (color, fill opacity,
// outline thickness) and puts the selected reference's 3D under it. See GameIDs.h and docs/RE-notes.md.

namespace MIC::Highlight
{
	// The console is open: highlight this reference (nullptr = nothing). Called on every panel refresh,
	// so geometry the reference gains (a new 3D, equipment) is picked up, and setting changes apply.
	void Show(RE::TESObjectREFR* a_ref);

	// The console is closed: take the highlight off.
	void Hide();
}
