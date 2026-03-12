#include "ExamineMenuAccessibility.h"
#include "ExamineMenuHook.h"
#include "SpeechManager.h"
#include <SRAL.h>

ExamineMenuAccessibility* ExamineMenuAccessibility::GetSingleton()
{
	static ExamineMenuAccessibility singleton;
	return &singleton;
}

void ExamineMenuAccessibility::OnExamineMenuOpened()
{
	m_isOpen = true;
	REX::INFO("ExamineMenuAccessibility: Menu opened");

	// Tell the hook the menu is open and reset its state
	// (Hook is installed at plugin startup, not here)
	ExamineMenuHook::GetSingleton()->SetMenuOpen(true);
	ExamineMenuHook::GetSingleton()->ResetState();

	// Defer initial announcement to next frame so Scaleform has time to populate
	if (auto* taskInterface = F4SE::GetTaskInterface()) {
		taskInterface->AddTask([this]() {
			if (!m_isOpen)
				return;

			auto ui = RE::UI::GetSingleton();
			if (!ui)
				return;

			auto menu = ui->GetMenu("ExamineMenu"sv);
			if (!menu)
				menu = ui->GetMenu("PowerArmorModMenu"sv);
			if (!menu)
				menu = ui->GetMenu("CookingMenu"sv);
			if (!menu)
				menu = ui->GetMenu("RobotModMenu"sv);
			if (!menu)
				return;

			// Announce workbench type
			auto*       examineMenu = static_cast<RE::ExamineMenu*>(menu.get());
			std::string workbenchType = GetWorkbenchType(examineMenu);
			SpeakText(workbenchType);

			// The AdvanceMovie hook will handle announcing the first item
		});
	}
}

void ExamineMenuAccessibility::OnExamineMenuClosed()
{
	m_isOpen = false;
	REX::INFO("ExamineMenuAccessibility: Menu closed");

	// Tell the hook the menu is closed
	ExamineMenuHook::GetSingleton()->SetMenuOpen(false);
}

void ExamineMenuAccessibility::Reset()
{
	m_isOpen = false;
}

std::string ExamineMenuAccessibility::GetWorkbenchType(RE::ExamineMenu* a_menu)
{
	if (!a_menu) {
		return "Workbench";
	}

	// Try to get workbench type from the furniture reference
	if (a_menu->workbenchRef) {
		auto* baseObj = a_menu->workbenchRef->GetObjectReference();
		if (baseObj) {
			auto* furniture = baseObj->As<RE::TESFurniture>();
			if (furniture) {
				auto wbType = furniture->wbData.type.get();
				switch (wbType) {
					case RE::WorkbenchData::Type::kWeapons:
						return "Weapon Workbench";
					case RE::WorkbenchData::Type::kArmor:
						return "Armor Workbench";
					case RE::WorkbenchData::Type::kAlchemy:
						return "Chemistry Station";
					case RE::WorkbenchData::Type::kPowerArmor:
						return "Power Armor Station";
					case RE::WorkbenchData::Type::kRobotMod:
						return "Robot Workbench";
					case RE::WorkbenchData::Type::kCreateObject:
						return "Crafting Station";
					default:
						break;
				}
			}
		}
	}

	return "Workbench";
}

void ExamineMenuAccessibility::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}
