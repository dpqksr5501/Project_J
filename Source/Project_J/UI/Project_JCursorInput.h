#pragma once
#include "InputCoreTypes.h"

/** Bare Left Alt toggles on release; modifier chords and focus loss cancel it. */
struct FProject_JCursorGesture
{
	void KeyDown(FKey Key, bool bRepeat, bool bOtherModifierDown = false)
	{
		if (Key == EKeys::LeftAlt) { if (!bRepeat) { bHeld = true; bChord = bOtherModifierDown; } }
		else if (bHeld && !bRepeat) bChord = true;
	}
	void MouseDown() { if (bHeld) bChord = true; }
	bool KeyUp(FKey Key)
	{
		if (Key != EKeys::LeftAlt) return false;
		const bool bToggle = bHeld && !bChord;
		Reset();
		return bToggle;
	}
	void Reset() { bHeld = bChord = false; }
private:
	bool bHeld = false;
	bool bChord = false;
};
