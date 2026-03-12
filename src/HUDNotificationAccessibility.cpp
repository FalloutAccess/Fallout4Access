#include "HUDNotificationAccessibility.h"
#include "HUDMenuHook.h"

HUDNotificationAccessibility* HUDNotificationAccessibility::GetSingleton()
{
	static HUDNotificationAccessibility singleton;
	return &singleton;
}

void HUDNotificationAccessibility::OnHUDMenuOpened()
{
	REX::INFO("HUDNotificationAccessibility: HUD opened");

	auto* hook = HUDMenuHook::GetSingleton();

	// Install the hook if not already installed
	hook->Install();

	// Reset state and enable checking
	hook->ResetState();
	hook->SetHUDOpen(true);
}

void HUDNotificationAccessibility::OnHUDMenuClosed()
{
	REX::INFO("HUDNotificationAccessibility: HUD closed");

	auto* hook = HUDMenuHook::GetSingleton();
	hook->SetHUDOpen(false);
}
