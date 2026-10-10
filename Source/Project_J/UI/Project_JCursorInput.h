#pragma once
#include "InputCoreTypes.h"

/** Bare Left Ctrl toggles on release; modifier chords and focus loss cancel it. */
struct FProject_JCursorGesture
{
	void KeyDown(FKey Key, bool bRepeat)
	{
		if (Key == EKeys::LeftControl) { if (!bRepeat) { bHeld = true; bChord = false; } }
		else if (bHeld && !bRepeat) bChord = true;
	}
	void MouseDown() { if (bHeld) bChord = true; }
	bool KeyUp(FKey Key)
	{
		if (Key != EKeys::LeftControl) return false;
		const bool bToggle = bHeld && !bChord;
		Reset();
		return bToggle;
	}
	void Reset() { bHeld = bChord = false; }
private:
	bool bHeld = false;
	bool bChord = false;
};
