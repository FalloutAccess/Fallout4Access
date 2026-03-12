#pragma once

#include "ItemIdentity.h"
#include "pch.h"
#include <RE/P/PipboyMenu.h>
#include <Scaleform/G/GFx_FunctionHandler.h>
#include <chrono>

// Hooks PipboyMenu's SWFToCodeFunctionHandler::Call to intercept AS-to-C++ callbacks
// This allows us to receive real-time selection updates from the Pip-Boy UI
class PipBoyCallbackHook
{
public:
	static PipBoyCallbackHook* GetSingleton();

	// Install/uninstall the vtable hook
	void Install();
	void Uninstall();

	// Check if hook is currently active
	bool IsInstalled() const { return m_installed; }

	// Reset duplicate announcement tracking (call when menu opens)
	void ResetLastAnnounced()
	{
		m_lastAnnouncedItemName.clear();
		m_lastAnnouncedWeight = 0.0f;
		m_lastAnnouncedValue = 0.0f;
		m_lastAnnouncedQuestIndex = -1;
		m_lastAnnouncedTime = {};
		ClearQuestState();
	}

	// Check if quest summary display state changed and announce accordingly.
	// Returns true if state changed. Called from Data page on non-navigation key presses.
	bool CheckSummaryStateChange();

	// Clear quest-specific state (when leaving Quests tab or Data page)
	void ClearQuestState();

private:
	PipBoyCallbackHook() = default;
	~PipBoyCallbackHook() = default;
	PipBoyCallbackHook(const PipBoyCallbackHook&) = delete;
	PipBoyCallbackHook(PipBoyCallbackHook&&) = delete;
	PipBoyCallbackHook& operator=(const PipBoyCallbackHook&) = delete;
	PipBoyCallbackHook& operator=(PipBoyCallbackHook&&) = delete;

	// The hooked Call function (vtable index 1)
	static void HookedCall(RE::PipboyMenu* a_this, const Scaleform::GFx::FunctionHandler::Params& a_params);

	// The hooked AdvanceMovie function (vtable index 4) - checks UI state changes per frame
	static void HookedAdvanceMovie(RE::PipboyMenu* a_this, float a_timeDelta, std::uint64_t a_time);

	// Callback handlers
	void HandleInvItemSelection(const Scaleform::GFx::FunctionHandler::Params& a_params);
	void HandleQuestSelection(const Scaleform::GFx::FunctionHandler::Params& a_params);
	void HandleWorkshopSelection(const Scaleform::GFx::FunctionHandler::Params& a_params);

	// Extract and announce inventory item identity from page reference (stats announced separately after original call)
	void AnnounceInventoryItem(const Scaleform::GFx::Value& a_pageRef);

	// Extract and announce quest name + status from callback params
	void AnnounceQuest(const Scaleform::GFx::Value& a_questEntry);

	// Read and announce quest objectives from the objectives array
	void AnnounceQuestObjectives(const Scaleform::GFx::Value& a_objectivesList);

	// Robustly read a boolean member from a Scaleform object (handles Number/Int/UInt types)
	static bool GetBoolMember(const Scaleform::GFx::Value& a_obj, const char* a_name);

	// Extract and announce workshop from callback params
	void AnnounceWorkshop(const Scaleform::GFx::Value& a_workshopEntry);

	// Utility
	void        SpeakText(const std::string& a_text);
	std::string CleanLocalizationString(const std::string& a_text);
	std::string TranslateKey(const std::string& a_key);
	std::string WideToNarrow(const std::wstring& a_wide);

	// Function IDs (populated on first hook call by examining known callbacks)
	// These are the integer IDs mapped from AS function names via MapCodeMethodToASFunction
	std::int32_t m_invItemSelectionID = -1;
	std::int32_t m_questSelectionID = -1;
	std::int32_t m_workshopSelectionID = -1;

	// State
	bool                                  m_installed = false;
	std::string                           m_lastAnnouncedItemName;       // For duplicate detection
	float                                 m_lastAnnouncedWeight = 0.0f;  // For stale InfoObj detection
	float                                 m_lastAnnouncedValue = 0.0f;   // For stale InfoObj detection
	std::int32_t                          m_lastAnnouncedQuestIndex = -1;
	std::chrono::steady_clock::time_point m_lastAnnouncedTime;  // For time-based dedup

	// Quest state tracking for summary/objectives detection
	Scaleform::GFx::Value m_questPageRef;                       // QuestsTab instance (arg[2] from onQuestSelection)
	Scaleform::GFx::Value m_objectivesListRef;                  // ObjectivesList_mc.entryList array
	std::string           m_currentQuestDescription;            // Current quest's description text
	std::string           m_lastAnnouncedQuestName;             // For deferred objectives dedup
	bool                  m_lastShowingSummary = false;         // Last known Description_tf.visible state
	bool                  m_pendingStatsAnnounce = false;       // Set by AnnounceInventoryItem, consumed by HookedCall
	bool                  m_pendingObjectivesAnnounce = false;  // Set by HandleQuestSelection, consumed by HookedCall

	// The vtable hooks
	static inline REL::THookVFT<void(RE::PipboyMenu*, const Scaleform::GFx::FunctionHandler::Params&)>* s_callHook = nullptr;
	static inline REL::THookVFT<void(RE::PipboyMenu*, float, std::uint64_t)>*                           s_advanceMovieHook = nullptr;
};
