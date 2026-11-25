#pragma once

// Highlights the reference selected in the console, using the game's own selection highlight.
//
// The game already highlights the console's selected reference in the color of its
// "aConsoleSelectedRefColor:Menu" setting, but that color is 0 by default: fully transparent, so
// nothing shows. This sets the color from the mod's settings. The game applies it when a reference
// is selected, so a new color shows from the next click on.

namespace MIC::Highlight
{
	// Game thread. Call after the settings are (re)loaded. With the highlight turned off, the game's
	// own value is left alone (or put back, if this mod changed it).
	void Apply();
}
