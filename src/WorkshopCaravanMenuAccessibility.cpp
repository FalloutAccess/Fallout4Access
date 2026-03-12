#include "WorkshopCaravanMenuAccessibility.h"
#include "LocalizationHelper.h"
#include "SpeechManager.h"

WorkshopCaravanMenuAccessibility* WorkshopCaravanMenuAccessibility::GetSingleton()
{
	static WorkshopCaravanMenuAccessibility singleton;
	return &singleton;
}

void WorkshopCaravanMenuAccessibility::Install()
{
	if (m_installed) {
		return;
	}

	// Hook Workshop_CaravanMenu::AdvanceMovie (vtable index 4)
	// No header in CommonLibF4, so use VTABLE ID directly
	static REL::THookVFT<void(RE::IMenu*, float, std::uint64_t)> hook(
		VTABLE::Workshop_CaravanMenu[0],
		4,
		HookedAdvanceMovie);
	if (hook.Enable()) {
		s_advanceMovieHook = &hook;
		REX::INFO("WorkshopCaravanMenuAccessibility: AdvanceMovie hook installed");
	} else {
		REX::ERROR("WorkshopCaravanMenuAccessibility: Failed to install AdvanceMovie hook");
	}

	m_installed = true;
}

void WorkshopCaravanMenuAccessibility::OnCaravanMenuOpened()
{
	m_menuOpen = true;
	m_lastSelectedIndex = -1;
	m_lastSpokenText.clear();
	m_frameDelay = FRAME_DELAY_AFTER_OPEN;

	REX::INFO("WorkshopCaravanMenuAccessibility: Menu opened");

	// Defer initial announcement
	if (auto* taskInterface = F4SE::GetTaskInterface()) {
		taskInterface->AddTask([this]() {
			if (!m_menuOpen)
				return;

			auto ui = RE::UI::GetSingleton();
			if (!ui)
				return;

			auto menu = ui->GetMenu("Workshop_CaravanMenu"sv);
			if (!menu)
				return;

			std::string header = GetHeaderText(menu.get());
			if (!header.empty()) {
				SpeakText("Supply Line: " + header);
			} else {
				SpeakText("Supply Line");
			}
		});
	}
}

void WorkshopCaravanMenuAccessibility::OnCaravanMenuClosed()
{
	m_menuOpen = false;
	REX::INFO("WorkshopCaravanMenuAccessibility: Menu closed");
}

void WorkshopCaravanMenuAccessibility::HookedAdvanceMovie(RE::IMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
	if (s_advanceMovieHook) {
		(*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
	}

	auto* hook = GetSingleton();
	if (hook->m_menuOpen) {
		hook->CheckStateChanges(a_this);
	}
}

void WorkshopCaravanMenuAccessibility::CheckStateChanges(RE::IMenu* a_menu)
{
	if (!a_menu)
		return;

	// Wait for initial frame delay
	if (m_frameDelay > 0) {
		m_frameDelay--;
		return;
	}

	CheckSelectionChange(a_menu);
}

void WorkshopCaravanMenuAccessibility::CheckSelectionChange(RE::IMenu* a_menu)
{
	std::int32_t currentIndex = GetSelectedIndex(a_menu);
	if (currentIndex < 0 || currentIndex == m_lastSelectedIndex) {
		return;
	}
	m_lastSelectedIndex = currentIndex;

	std::string settlement = GetSelectedSettlement(a_menu);
	if (!settlement.empty()) {
		std::int32_t total = GetListSize(a_menu);
		std::string  announcement = settlement;
		if (total > 0) {
			announcement += ", " + std::to_string(currentIndex + 1) + " of " + std::to_string(total);
		}
		SpeakText(announcement);
	}
}

std::string WorkshopCaravanMenuAccessibility::GetHeaderText(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie)
		return "";

	Scaleform::GFx::Value val;
	if (a_menu->uiMovie->GetVariable(&val, HEADER_TEXT_PATH) && val.IsString()) {
		return LocalizationHelper::CleanString(val.GetString());
	}
	return "";
}

std::string WorkshopCaravanMenuAccessibility::GetSelectedSettlement(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie)
		return "";

	std::int32_t index = GetSelectedIndex(a_menu);
	if (index < 0)
		return "";

	Scaleform::GFx::Value entryList;
	if (!a_menu->uiMovie->GetVariable(&entryList, ENTRY_LIST_PATH) || !entryList.IsArray()) {
		return "";
	}

	if (static_cast<std::uint32_t>(index) >= entryList.GetArraySize()) {
		return "";
	}

	Scaleform::GFx::Value entry;
	if (!entryList.GetElement(static_cast<std::uint32_t>(index), &entry) || !entry.IsObject()) {
		return "";
	}

	std::string           name;
	Scaleform::GFx::Value textVal;
	if (entry.GetMember("text", &textVal) && textVal.IsString()) {
		name = LocalizationHelper::CleanString(textVal.GetString());
	}

	// Check if disabled (can't send caravan here)
	Scaleform::GFx::Value disabledVal;
	if (entry.GetMember("disabled", &disabledVal) && disabledVal.IsBoolean()) {
		if (disabledVal.GetBoolean()) {
			name += " (unavailable)";
		}
	}

	return name;
}

std::int32_t WorkshopCaravanMenuAccessibility::GetSelectedIndex(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie)
		return -1;

	Scaleform::GFx::Value val;
	if (a_menu->uiMovie->GetVariable(&val, SELECTED_INDEX_PATH)) {
		if (val.IsInt())
			return val.GetInt();
		if (val.IsNumber())
			return static_cast<std::int32_t>(val.GetNumber());
		if (val.IsUInt())
			return static_cast<std::int32_t>(val.GetUInt());
	}
	return -1;
}

std::int32_t WorkshopCaravanMenuAccessibility::GetListSize(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie)
		return 0;

	Scaleform::GFx::Value entryList;
	if (!a_menu->uiMovie->GetVariable(&entryList, ENTRY_LIST_PATH) || !entryList.IsArray()) {
		return 0;
	}
	return static_cast<std::int32_t>(entryList.GetArraySize());
}

void WorkshopCaravanMenuAccessibility::SpeakText(const std::string& a_text)
{
	if (a_text.empty())
		return;

	// Deduplicate
	if (a_text == m_lastSpokenText)
		return;
	m_lastSpokenText = a_text;

	SpeechManager::GetSingleton()->Speak(a_text);
}
