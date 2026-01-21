#pragma once

#include "pch.h"

// Handles accessibility for HUD notifications (messages, quest updates, objective updates, XP)
// Uses HUDMenuHook to check for notification changes every frame via AdvanceMovie hook
class HUDNotificationAccessibility
{
public:
    static HUDNotificationAccessibility* GetSingleton();

    // Lifecycle (called from MenuAccessibility)
    void OnHUDMenuOpened();
    void OnHUDMenuClosed();

private:
    HUDNotificationAccessibility() = default;
    ~HUDNotificationAccessibility() = default;
    HUDNotificationAccessibility(const HUDNotificationAccessibility&) = delete;
    HUDNotificationAccessibility(HUDNotificationAccessibility&&) = delete;
    HUDNotificationAccessibility& operator=(const HUDNotificationAccessibility&) = delete;
    HUDNotificationAccessibility& operator=(HUDNotificationAccessibility&&) = delete;
};
