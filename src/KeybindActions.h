#pragma once

#include "pch.h"
#include <string_view>

// Dispatches one of our named keybind actions (e.g. "next_object",
// "announce_stats"). Called from MCMKeybindDispatcher in both gameplay
// (ObjectScanner) and menu (MenuAccessibility) input contexts.
//
// Action IDs match the `id` (minus `:section` suffix) in
// Data/MCM/Config/fallout4access/config.json.
//
// Returns true if the action was recognized. An unknown id is a no-op
// and returns false — callers can treat that as "not ours, keep looking".
namespace KeybindActions
{
	bool Dispatch(std::string_view a_actionId);
}
