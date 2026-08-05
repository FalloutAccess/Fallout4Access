#include "PipBoyCallbackHook.h"
#include "ItemStatFormatter.h"
#include "PipBoyMenuAccessibility.h"
#include "SpeechManager.h"
#include <F4SE/F4SE.h>
#include <RE/B/BSFixedString.h>
#include <RE/B/BSScaleformManager.h>
#include <RE/B/BSScaleformTranslator.h>
#include <RE/B/BSTranslator.h>
#include <chrono>
#include <cmath>
#include <cstdlib>

PipBoyCallbackHook* PipBoyCallbackHook::GetSingleton()
{
	static PipBoyCallbackHook singleton;
	return &singleton;
}

void PipBoyCallbackHook::Install()
{
	if (m_installed) {
		REX::DEBUG("PipBoyCallbackHook: Already installed");
		return;
	}

	// Hook PipboyMenu's Call function (vtable index 1, inherited from SWFToCodeFunctionHandler)
	// VTABLE::PipboyMenu[0] is the primary vtable (SWFToCodeFunctionHandler/IMenu vtable)
	// Index 1 is the Call virtual function
	static REL::THookVFT<void(RE::PipboyMenu*, const Scaleform::GFx::FunctionHandler::Params&)> hook(
		VTABLE::PipboyMenu[0],  // First vtable entry
		1,                      // Call is at index 1
		HookedCall);

	if (hook.Enable()) {
		s_callHook = &hook;
		m_installed = true;
		REX::INFO("PipBoyCallbackHook: Call hook installed successfully");
	} else {
		REX::ERROR("PipBoyCallbackHook: Failed to install Call hook");
	}

	// Hook PipboyMenu's AdvanceMovie (vtable index 4) for per-frame state detection
	static REL::THookVFT<void(RE::PipboyMenu*, float, std::uint64_t)> amHook(
		VTABLE::PipboyMenu[0],
		4,
		HookedAdvanceMovie);

	if (amHook.Enable()) {
		s_advanceMovieHook = &amHook;
		REX::INFO("PipBoyCallbackHook: AdvanceMovie hook installed successfully");
	} else {
		REX::ERROR("PipBoyCallbackHook: Failed to install AdvanceMovie hook");
	}
}

void PipBoyCallbackHook::Uninstall()
{
	if (!m_installed || !s_callHook) {
		return;
	}

	if (s_callHook->Disable()) {
		m_installed = false;
		REX::INFO("PipBoyCallbackHook: Hook uninstalled");
	}
}

void PipBoyCallbackHook::HookedCall(RE::PipboyMenu* a_this, const Scaleform::GFx::FunctionHandler::Params& a_params)
{
	auto* hook = GetSingleton();

	// Get function ID from userData
	auto funcID = reinterpret_cast<std::intptr_t>(a_params.userData);

	// Log callback for debugging (only first few times to avoid spam)
	static int logCount = 0;
	if (logCount < 100) {
		// Log arg types for 4-arg callbacks to debug identification
		if (a_params.argCount == 4 && a_params.args) {
			REX::DEBUG("PipBoyCallbackHook: Call funcID={}, argCount=4, arg0[Int={},UInt={},Num={}], arg1[Arr={},Obj={}]",
				funcID,
				a_params.args[0].IsInt(), a_params.args[0].IsUInt(), a_params.args[0].IsNumber(),
				a_params.args[1].IsArray(), a_params.args[1].IsObject());
		} else {
			REX::DEBUG("PipBoyCallbackHook: Call received - funcID={}, argCount={}", funcID, a_params.argCount);
		}
		logCount++;
	}

	// Try to identify callback by function ID or argument signature
	// onInvItemSelection: 4 args (int selectedIndex, Array InfoObj, Array selectedInfoObj, DisplayObject page)
	// Note: IsNumber() only matches doubles, use IsInt()/IsUInt() for integers
	bool arg0IsNumeric = a_params.args && (a_params.args[0].IsNumber() || a_params.args[0].IsInt() || a_params.args[0].IsUInt());
	bool arg1IsArray = a_params.args && a_params.args[1].IsArray();

	if (a_params.argCount == 4 && arg0IsNumeric && arg1IsArray) {
		// Cache the function ID for future fast matching
		if (hook->m_invItemSelectionID < 0) {
			hook->m_invItemSelectionID = static_cast<std::int32_t>(funcID);
			REX::INFO("PipBoyCallbackHook: Identified onInvItemSelection funcID={}", funcID);
		}

		if (funcID == hook->m_invItemSelectionID) {
			hook->HandleInvItemSelection(a_params);
		}
	}
	// onQuestSelection: 3 args (Array objectivesList, Object selectedQuest, DisplayObject page)
	else if (a_params.argCount == 3 && a_params.args && a_params.args[0].IsArray() && a_params.args[1].IsObject()) {
		if (hook->m_questSelectionID < 0) {
			hook->m_questSelectionID = static_cast<std::int32_t>(funcID);
			REX::INFO("PipBoyCallbackHook: Identified onQuestSelection funcID={}", funcID);
		}

		if (funcID == hook->m_questSelectionID) {
			hook->HandleQuestSelection(a_params);
		}
	}

	// Call the original function to maintain game behavior
	if (s_callHook) {
		(*s_callHook)(a_this, a_params);
	}

	// After original call: Scaleform data is now updated by the game.
	// Read it immediately while the values are still valid (same frame).
	if (funcID == hook->m_invItemSelectionID && hook->m_pendingStatsAnnounce) {
		hook->m_pendingStatsAnnounce = false;
		// InfoObj (args[1]) now has the correct item's stats
		if (a_params.args && a_params.args[1].IsArray()) {
			auto stats = ItemStatFormatter::ParseInfoObj(a_params.args[1]);
			if (!stats.empty()) {
				std::string statsAnnouncement = ItemStatFormatter::BuildStatAnnouncement(stats);
				if (!statsAnnouncement.empty()) {
					hook->SpeakText(statsAnnouncement.substr(2));  // Remove leading ", "
				}
			}
		}
	}
	if (funcID == hook->m_questSelectionID && hook->m_pendingObjectivesAnnounce) {
		hook->m_pendingObjectivesAnnounce = false;
		hook->AnnounceQuestObjectives(hook->m_objectivesListRef);
	}
}

void PipBoyCallbackHook::HookedAdvanceMovie(RE::PipboyMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
	// Call original first so the game updates its state
	if (s_advanceMovieHook) {
		(*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
	}

	// Check for summary state change (lightweight boolean comparison, no logging)
	GetSingleton()->CheckSummaryStateChange();

	// Per-frame polling for active page (Map page needs this for mouse selection detection)
	PipBoyMenuAccessibility::GetSingleton()->CheckSelection();
}

void PipBoyCallbackHook::HandleInvItemSelection(const Scaleform::GFx::FunctionHandler::Params& a_params)
{
	// arg[0] = selectedIndex (int) - index in filtered list, NOT used directly
	// arg[1] = ItemCard_mc.InfoObj (Array of stat entries)
	// arg[2] = PaperDoll_mc.selectedInfoObj (Array of booleans for armor slots)
	// arg[3] = page reference (Pipboy_InvPage instance with List_mc.selectedEntry)

	REX::DEBUG("PipBoyCallbackHook: onInvItemSelection callback received");

	// arg[3] is the page reference (Pipboy_InvPage) - use this to get selectedEntry
	const auto& pageRef = a_params.args[3];
	if (!pageRef.IsObject()) {
		REX::DEBUG("PipBoyCallbackHook: Page reference (arg[3]) is not an object");
		return;
	}

	AnnounceInventoryItem(pageRef);
}

void PipBoyCallbackHook::HandleQuestSelection(const Scaleform::GFx::FunctionHandler::Params& a_params)
{
	// arg[0] = objectivesList (Array - empty now, populated by original function after we return)
	// arg[1] = selectedQuest (Object with quest data)
	// arg[2] = page reference (QuestsTab instance)

	const auto& questEntry = a_params.args[1];
	if (!questEntry.IsObject()) {
		return;
	}

	// Store page reference for summary state detection
	if (a_params.argCount > 2 && a_params.args[2].IsObject()) {
		m_questPageRef = a_params.args[2];
	}

	// Store objectives array reference (will be populated by original function)
	m_objectivesListRef = a_params.args[0];

	// Store description for summary mode announcement
	m_currentQuestDescription.clear();
	Scaleform::GFx::Value descProp;
	if (questEntry.GetMember("description", &descProp) && descProp.IsString()) {
		m_currentQuestDescription = CleanLocalizationString(descProp.GetString());
	}

	// Reset summary state on new quest selection (game resets _ShowingSummary too)
	m_lastShowingSummary = false;

	// Announce quest name + status immediately
	AnnounceQuest(questEntry);

	// Signal that objectives should be announced after the original Call populates them.
	// We don't defer to next frame (which would cause use-after-free on the Scaleform value).
	// Instead, HookedCall reads objectives right after the original function returns.
	m_pendingObjectivesAnnounce = true;
}

void PipBoyCallbackHook::HandleWorkshopSelection(const Scaleform::GFx::FunctionHandler::Params& a_params)
{
	// Similar pattern - extract workshop data and announce
	if (a_params.argCount < 2)
		return;

	const auto& workshopEntry = a_params.args[1];
	if (!workshopEntry.IsObject()) {
		return;
	}

	AnnounceWorkshop(workshopEntry);
}

void PipBoyCallbackHook::AnnounceInventoryItem(const Scaleform::GFx::Value& a_pageRef)
{
	// Extract item identity from page.List_mc.selectedEntry (reliable, synchronous)
	ItemIdentity identity;

	Scaleform::GFx::Value listMc, selectedEntry;
	if (a_pageRef.GetMember("List_mc", &listMc) && listMc.IsObject()) {
		if (listMc.GetMember("selectedEntry", &selectedEntry) && selectedEntry.IsObject()) {
			Scaleform::GFx::Value prop;

			if (selectedEntry.GetMember("text", &prop) && prop.IsString()) {
				identity.name = CleanLocalizationString(prop.GetString());
			}

			if (selectedEntry.GetMember("count", &prop)) {
				if (prop.IsInt())
					identity.count = prop.GetInt();
				else if (prop.IsNumber())
					identity.count = static_cast<std::int32_t>(prop.GetNumber());
				else if (prop.IsUInt())
					identity.count = static_cast<std::int32_t>(prop.GetUInt());
			}

			if (selectedEntry.GetMember("equipState", &prop)) {
				if (prop.IsInt())
					identity.equipState = prop.GetInt();
				else if (prop.IsNumber())
					identity.equipState = static_cast<std::int32_t>(prop.GetNumber());
				else if (prop.IsUInt())
					identity.equipState = static_cast<std::int32_t>(prop.GetUInt());
			}

			if (selectedEntry.GetMember("isLegendary", &prop) && prop.IsBoolean()) {
				identity.isLegendary = prop.GetBoolean();
			}

			if (selectedEntry.GetMember("favorite", &prop)) {
				if (prop.IsBoolean()) {
					identity.isFavorite = prop.GetBoolean();
				} else if (prop.IsNumber() || prop.IsInt() || prop.IsUInt()) {
					int favVal = 0;
					if (prop.IsInt())
						favVal = prop.GetInt();
					else if (prop.IsNumber())
						favVal = static_cast<int>(prop.GetNumber());
					else if (prop.IsUInt())
						favVal = static_cast<int>(prop.GetUInt());
					identity.isFavorite = (favVal > 0);
				}
			}

			if (selectedEntry.GetMember("taggedForSearch", &prop) && prop.IsBoolean()) {
				identity.isTagged = prop.GetBoolean();
			}

			REX::DEBUG("PipBoyCallbackHook: Extracted identity: '{}'", identity.name);
		} else {
			REX::DEBUG("PipBoyCallbackHook: List_mc.selectedEntry access failed");
			return;
		}
	} else {
		REX::DEBUG("PipBoyCallbackHook: List_mc access from page reference failed");
		return;
	}

	if (!identity.IsValid()) {
		REX::DEBUG("PipBoyCallbackHook: Could not extract item identity");
		return;
	}

	// Time-based dedup: skip if same item announced within 100ms
	// Game fires onInvItemSelection 3+ times per item change
	auto now = std::chrono::steady_clock::now();
	if (identity.name == m_lastAnnouncedItemName) {
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastAnnouncedTime);
		if (elapsed.count() < 100) {
			REX::DEBUG("PipBoyCallbackHook: Dedup - same item '{}' within {}ms", identity.name, elapsed.count());
			return;
		}
	}
	m_lastAnnouncedItemName = identity.name;
	m_lastAnnouncedTime = now;

	// Announce identity right away (responsive feedback)
	SpeakText(identity.BuildBasicAnnouncement());
	REX::DEBUG("PipBoyCallbackHook: Announced identity immediately: '{}'", identity.name);

	// Signal that stats should be announced after the original Call updates the InfoObj.
	// The InfoObj array contains stale (previous item) data at this point.
	// HookedCall will parse stats after the original function populates correct data.
	m_pendingStatsAnnounce = true;
}

void PipBoyCallbackHook::AnnounceQuest(const Scaleform::GFx::Value& a_questEntry)
{
	std::string questName;
	bool        isDivider = GetBoolMember(a_questEntry, "isDivider");

	if (isDivider) {
		// Divider separates active from completed quests - no text in the game
		Scaleform::GFx::Value prop;
		if (a_questEntry.GetMember("text", &prop) && prop.IsString()) {
			questName = CleanLocalizationString(prop.GetString());
		}
		if (!questName.empty()) {
			SpeakText(questName);
		}
		m_lastAnnouncedQuestName.clear();
		return;
	}

	// Get quest name
	Scaleform::GFx::Value prop;
	if (a_questEntry.GetMember("text", &prop) && prop.IsString()) {
		questName = CleanLocalizationString(prop.GetString());
	}

	if (questName.empty()) {
		return;
	}

	m_lastAnnouncedQuestName = questName;

	// Read status fields
	// Note: "completed" property is undefined on quest objects (game doesn't populate it).
	// Status is determined by "enabled" (false = done) and "active" (true = tracked).
	bool isActive = GetBoolMember(a_questEntry, "active");
	bool isEnabled = GetBoolMember(a_questEntry, "enabled");

	// Build announcement: name + status
	std::string announcement = questName;

	if (isActive) {
		announcement += ", active";
	} else if (!isEnabled) {
		announcement += ", completed";
	}

	SpeakText(announcement);
}

bool PipBoyCallbackHook::GetBoolMember(const Scaleform::GFx::Value& a_obj, const char* a_name)
{
	Scaleform::GFx::Value prop;
	if (a_obj.GetMember(a_name, &prop)) {
		if (prop.IsBoolean())
			return prop.GetBoolean();
		if (prop.IsNumber())
			return prop.GetNumber() != 0.0;
		if (prop.IsInt())
			return prop.GetInt() != 0;
		if (prop.IsUInt())
			return prop.GetUInt() != 0;
	}
	return false;
}

void PipBoyCallbackHook::AnnounceQuestObjectives(const Scaleform::GFx::Value& a_objectivesList)
{
	if (!a_objectivesList.IsArray()) {
		return;
	}

	std::uint32_t count = a_objectivesList.GetArraySize();
	if (count == 0) {
		return;
	}

	std::string announcement;
	for (std::uint32_t i = 0; i < count; i++) {
		Scaleform::GFx::Value entry;
		if (!a_objectivesList.GetElement(i, &entry) || !entry.IsObject()) {
			continue;
		}

		if (GetBoolMember(entry, "isDivider")) {
			continue;
		}

		Scaleform::GFx::Value prop;
		std::string           text;
		if (entry.GetMember("text", &prop) && prop.IsString()) {
			text = CleanLocalizationString(prop.GetString());
		}
		if (text.empty()) {
			continue;
		}

		bool completed = GetBoolMember(entry, "completed");

		if (!announcement.empty()) {
			announcement += ". ";
		}
		announcement += text;
		if (completed) {
			announcement += ", completed";
		}
	}

	if (!announcement.empty()) {
		SpeakText(announcement);
	}
}

bool PipBoyCallbackHook::CheckSummaryStateChange()
{
	if (!m_questPageRef.IsObject()) {
		return false;
	}

	// Check Description_tf.visible on the QuestsTab page reference
	Scaleform::GFx::Value descTf;
	if (!m_questPageRef.GetMember("Description_tf", &descTf) || !descTf.IsObject()) {
		return false;
	}

	Scaleform::GFx::Value visibleProp;
	if (!descTf.GetMember("visible", &visibleProp) || !visibleProp.IsBoolean()) {
		return false;
	}

	bool isShowingSummary = visibleProp.GetBoolean();

	if (isShowingSummary != m_lastShowingSummary) {
		m_lastShowingSummary = isShowingSummary;

		if (isShowingSummary && !m_currentQuestDescription.empty()) {
			// Summary toggled on - announce description
			SpeakText(m_currentQuestDescription);
		} else if (!isShowingSummary && m_objectivesListRef.IsArray()) {
			// Summary toggled off - re-announce objectives
			AnnounceQuestObjectives(m_objectivesListRef);
		}

		return true;
	}

	return false;
}

void PipBoyCallbackHook::ClearQuestState()
{
	m_questPageRef = Scaleform::GFx::Value();
	m_objectivesListRef = Scaleform::GFx::Value();
	m_currentQuestDescription.clear();
	m_lastAnnouncedQuestName.clear();
	m_lastShowingSummary = false;
}

void PipBoyCallbackHook::AnnounceWorkshop(const Scaleform::GFx::Value& a_workshopEntry)
{
	std::string name;
	float       happiness = 0.0f;
	int         population = 0;

	Scaleform::GFx::Value prop;

	// Get workshop name
	if (a_workshopEntry.GetMember("text", &prop) && prop.IsString()) {
		name = CleanLocalizationString(prop.GetString());
	}

	// Get happiness
	if (a_workshopEntry.GetMember("happiness", &prop)) {
		if (prop.IsNumber())
			happiness = static_cast<float>(prop.GetNumber());
		else if (prop.IsInt())
			happiness = static_cast<float>(prop.GetInt());
	}

	// Get population
	if (a_workshopEntry.GetMember("population", &prop)) {
		if (prop.IsInt())
			population = prop.GetInt();
		else if (prop.IsNumber())
			population = static_cast<int>(prop.GetNumber());
	}

	if (name.empty()) {
		return;
	}

	// Build announcement
	std::string announcement = name;

	if (population > 0) {
		announcement += ", " + std::to_string(population) + " settlers";
	}

	if (happiness > 0) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%.0f%%", happiness);
		announcement += ", " + std::string(buf) + " happiness";
	}

	SpeakText(announcement);
}

void PipBoyCallbackHook::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}

std::string PipBoyCallbackHook::CleanLocalizationString(const std::string& a_text)
{
	if (a_text.empty()) {
		return a_text;
	}

	std::string result = a_text;

	// Remove $ prefix used for localization keys
	if (result[0] == '$') {
		result = result.substr(1);
	}

	// Replace underscores and hyphens with spaces for better speech
	for (char& c : result) {
		if (c == '_' || c == '-') {
			c = ' ';
		}
	}

	return result;
}

std::string PipBoyCallbackHook::TranslateKey(const std::string& a_key)
{
	auto* sfManager = RE::BSScaleformManager::GetSingleton();
	if (!sfManager) {
		return CleanLocalizationString(a_key);
	}

	auto* translator = sfManager->GetTranslator();
	if (!translator) {
		return CleanLocalizationString(a_key);
	}

	// Convert to wide string for lookup
	std::wstring         wideKey(a_key.begin(), a_key.end());
	RE::BSFixedStringWCS lookupKey(wideKey.c_str());

	auto& translationMap = translator->translator.translationMap;
	auto  it = translationMap.find(lookupKey);

	if (it != translationMap.end()) {
		std::wstring wideResult(it->second.c_str());
		return WideToNarrow(wideResult);
	}

	return CleanLocalizationString(a_key);
}

std::string PipBoyCallbackHook::WideToNarrow(const std::wstring& a_wide)
{
	if (a_wide.empty())
		return "";
	// Use Windows API via REX for proper UTF-8 conversion
	std::string result;
	result.reserve(a_wide.size() * 3);  // UTF-8 can be up to 3 bytes per wchar
	for (wchar_t wc : a_wide) {
		if (wc < 0x80) {
			result += static_cast<char>(wc);
		} else if (wc < 0x800) {
			result += static_cast<char>(0xC0 | (wc >> 6));
			result += static_cast<char>(0x80 | (wc & 0x3F));
		} else {
			result += static_cast<char>(0xE0 | (wc >> 12));
			result += static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
			result += static_cast<char>(0x80 | (wc & 0x3F));
		}
	}
	return result;
}
