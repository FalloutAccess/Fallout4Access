#include "ExamineConfirmMenuHook.h"
#include "LocalizationHelper.h"
#include "SpeechManager.h"

ExamineConfirmMenuHook* ExamineConfirmMenuHook::GetSingleton()
{
	static ExamineConfirmMenuHook singleton;
	return &singleton;
}

void ExamineConfirmMenuHook::Install()
{
	if (m_installed) {
		return;
	}

	// Hook ExamineConfirmMenu::AdvanceMovie (vtable index 4)
	static REL::THookVFT<void(RE::ExamineConfirmMenu*, float, std::uint64_t)> hook(
		VTABLE::ExamineConfirmMenu[0],
		4,  // AdvanceMovie is at index 4
		HookedAdvanceMovie);

	if (hook.Enable()) {
		s_advanceMovieHook = &hook;
		m_installed = true;
		REX::INFO("ExamineConfirmMenuHook: AdvanceMovie hook installed successfully");
	} else {
		REX::ERROR("ExamineConfirmMenuHook: Failed to install AdvanceMovie hook");
	}
}

void ExamineConfirmMenuHook::ResetState()
{
	m_announced = false;
	m_framesSinceOpen = 0;
}

void ExamineConfirmMenuHook::HookedAdvanceMovie(RE::ExamineConfirmMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
	// Call original function first
	if (s_advanceMovieHook) {
		(*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
	}

	auto* hook = GetSingleton();
	if (!hook->m_menuOpen)
		return;

	hook->m_framesSinceOpen++;

	// Stop polling once we've announced or exceeded retry window
	if (hook->m_announced || hook->m_framesSinceOpen > MAX_RETRY_FRAMES)
		return;

	hook->CheckContent(a_this);
}

void ExamineConfirmMenuHook::CheckContent(RE::ExamineConfirmMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie)
		return;

	std::string questionText = GetQuestionText(a_menu);
	if (questionText.empty())
		return;

	// Build full announcement: question + component list
	std::string announcement = LocalizationHelper::Translate(questionText);

	std::string components = GetComponentList(a_menu);
	if (!components.empty()) {
		announcement += ". " + components;
	}

	SpeakText(announcement);
	m_announced = true;
}

std::string ExamineConfirmMenuHook::GetQuestionText(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return "";
	}

	Scaleform::GFx::Value value;
	if (a_menu->uiMovie->GetVariable(&value, QUESTION_TEXT_PATH)) {
		if (value.IsString()) {
			return value.GetString();
		}
	}

	return "";
}

std::string ExamineConfirmMenuHook::GetComponentList(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return "";
	}

	// Get the ConfirmPanel (Confirm_mc)
	Scaleform::GFx::Value confirmPanel;
	if (!a_menu->uiMovie->GetVariable(&confirmPanel, CONFIRM_PANEL_PATH) || !confirmPanel.IsObject()) {
		return "";
	}

	// Get number of children on the panel
	Scaleform::GFx::Value numChildrenVal;
	if (!confirmPanel.GetMember("numChildren", &numChildrenVal)) {
		return "";
	}

	std::uint32_t numChildren = 0;
	if (numChildrenVal.IsUInt())
		numChildren = numChildrenVal.GetUInt();
	else if (numChildrenVal.IsInt())
		numChildren = static_cast<std::uint32_t>(numChildrenVal.GetInt());
	else if (numChildrenVal.IsNumber())
		numChildren = static_cast<std::uint32_t>(numChildrenVal.GetNumber());

	if (numChildren == 0) {
		return "";
	}

	std::string result;

	// Iterate children to find ConfirmPanelComponentSourceEntry instances
	// (they have "itemName" property, unlike static children like BGRect_mc)
	for (std::uint32_t i = 0; i < numChildren; ++i) {
		Scaleform::GFx::Value child;
		Scaleform::GFx::Value args[1];
		args[0] = static_cast<std::uint32_t>(i);

		if (!confirmPanel.Invoke("getChildAt", &child, args, 1) || !child.IsObject()) {
			continue;
		}

		// Check if this is a source entry by looking for "itemName" property
		Scaleform::GFx::Value itemNameVal;
		if (!child.GetMember("itemName", &itemNameVal) || !itemNameVal.IsString()) {
			continue;
		}

		// Read the formatted source text (e.g., "Desk Fan") and component entries (e.g., "Bolt (2)")
		// Format: "Component from Source" (e.g., "Bolt (2) from Desk Fan")
		Scaleform::GFx::Value textFieldVal, textVal;
		if (child.GetMember("textField", &textFieldVal) && textFieldVal.IsObject()) {
			if (textFieldVal.GetMember("text", &textVal) && textVal.IsString()) {
				std::string sourceText = textVal.GetString();
				if (!sourceText.empty()) {
					// Read component entries under this source
					std::string compTexts = GetComponentEntries(child);
					if (!compTexts.empty()) {
						if (!result.empty()) {
							result += ". ";
						}
						result += compTexts;
						result += " from ";
						result += sourceText;
					}
				}
			}
		}
	}

	return result;
}

std::string ExamineConfirmMenuHook::GetComponentEntries(Scaleform::GFx::Value& a_sourceEntry)
{
	Scaleform::GFx::Value numChildrenVal;
	if (!a_sourceEntry.GetMember("numChildren", &numChildrenVal)) {
		return "";
	}

	std::uint32_t numChildren = 0;
	if (numChildrenVal.IsUInt())
		numChildren = numChildrenVal.GetUInt();
	else if (numChildrenVal.IsInt())
		numChildren = static_cast<std::uint32_t>(numChildrenVal.GetInt());
	else if (numChildrenVal.IsNumber())
		numChildren = static_cast<std::uint32_t>(numChildrenVal.GetNumber());

	std::string result;

	for (std::uint32_t j = 0; j < numChildren; ++j) {
		Scaleform::GFx::Value child;
		Scaleform::GFx::Value args[1];
		args[0] = static_cast<std::uint32_t>(j);

		if (!a_sourceEntry.Invoke("getChildAt", &child, args, 1) || !child.IsObject()) {
			continue;
		}

		// Check if this is a component entry by looking for "componentName" property
		Scaleform::GFx::Value compNameVal;
		if (!child.GetMember("componentName", &compNameVal) || !compNameVal.IsString()) {
			continue;
		}

		// Read the formatted component text (e.g., "Glue (2/4)")
		Scaleform::GFx::Value textFieldVal, textVal;
		if (child.GetMember("textField", &textFieldVal) && textFieldVal.IsObject()) {
			if (textFieldVal.GetMember("text", &textVal) && textVal.IsString()) {
				std::string compText = textVal.GetString();
				if (!compText.empty()) {
					if (!result.empty()) {
						result += ", ";
					}
					result += compText;
				}
			}
		}
	}

	return result;
}

void ExamineConfirmMenuHook::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}
