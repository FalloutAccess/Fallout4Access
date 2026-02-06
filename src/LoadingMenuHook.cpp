#include "LoadingMenuHook.h"
#include "SpeechManager.h"
#include <RE/B/BGSSaveLoadManager.h>

LoadingMenuHook* LoadingMenuHook::GetSingleton()
{
    static LoadingMenuHook singleton;
    return &singleton;
}

void LoadingMenuHook::Install()
{
    if (m_installed) {
        return;
    }

    // Hook LoadingMenu::AdvanceMovie (vtable index 4)
    static REL::HookVFT<void(RE::LoadingMenu*, float, std::uint64_t)> hook(
        VTABLE::LoadingMenu[0],
        4,  // AdvanceMovie is at index 4
        HookedAdvanceMovie
    );

    if (hook.Enable()) {
        s_advanceMovieHook = &hook;
        m_installed = true;
        REX::INFO("LoadingMenuHook: AdvanceMovie hook installed successfully");
    } else {
        REX::ERROR("LoadingMenuHook: Failed to install AdvanceMovie hook");
    }
}

void LoadingMenuHook::ResetState()
{
    m_initialAnnouncementDone = false;
    m_framesSinceOpen = 0;
    m_lastTipText.clear();
}

void LoadingMenuHook::HookedAdvanceMovie(RE::LoadingMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
    // Call original function first to let the game update state
    if (s_advanceMovieHook) {
        (*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
    }

    // Then check for state changes
    auto* hook = GetSingleton();
    if (hook->m_menuOpen) {
        hook->m_framesSinceOpen++;

        // Try initial announcement within retry window
        if (!hook->m_initialAnnouncementDone && hook->m_framesSinceOpen <= MAX_RETRY_FRAMES) {
            hook->AnnounceInitialContent(a_this);
        }

        // Check for tip rotation (after initial announcement)
        if (hook->m_initialAnnouncementDone) {
            hook->CheckStateChanges(a_this);
        }
    }
}

void LoadingMenuHook::AnnounceInitialContent(RE::LoadingMenu* a_menu)
{
    if (!a_menu) return;

    // Skip announcement in minimal mode (black screen only)
    if (IsMinimalMode(a_menu)) {
        m_initialAnnouncementDone = true;
        return;
    }

    // Get all content components
    std::string level = GetPlayerLevel(a_menu);
    std::string vdsg = GetVDSGNumber(a_menu);
    std::string tip = GetTipText(a_menu);

    // Wait for at least some content to be populated
    if (level.empty() && vdsg.empty() && tip.empty()) {
        // Content not ready yet, will retry next frame
        return;
    }

    // Build composite announcement
    std::string announcement;

    if (!level.empty()) {
        announcement += level;

        // Add XP progress percentage after level
        std::int32_t xpPercent = GetXPProgressPercent();
        if (xpPercent >= 0) {
            announcement += ", " + std::to_string(xpPercent) + "% to next level";
        }
    }

    if (!vdsg.empty()) {
        if (!announcement.empty()) {
            announcement += ". ";
        }
        announcement += vdsg;
    }

    if (!tip.empty()) {
        if (!announcement.empty()) {
            announcement += ". ";
        }
        announcement += tip;
    }

    if (!announcement.empty()) {
        SpeechManager::GetSingleton()->Speak(announcement);
        m_lastTipText = tip;
        m_initialAnnouncementDone = true;
    }
}

void LoadingMenuHook::CheckStateChanges(RE::LoadingMenu* a_menu)
{
    if (!a_menu) return;

    // Skip checks in minimal mode
    if (IsMinimalMode(a_menu)) {
        return;
    }

    // Check for tip rotation
    std::string currentTip = GetTipText(a_menu);
    if (!currentTip.empty() && currentTip != m_lastTipText) {
        m_lastTipText = currentTip;
        SpeechManager::GetSingleton()->Speak(currentTip);
    }
}

std::string LoadingMenuHook::GetTipText(RE::LoadingMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return "";
    }

    Scaleform::GFx::Value value;
    if (a_menu->uiMovie->GetVariable(&value, TIP_TEXT_PATH)) {
        if (value.IsString()) {
            return value.GetString();
        }
    }

    return "";
}

std::string LoadingMenuHook::GetVDSGNumber(RE::LoadingMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return "";
    }

    Scaleform::GFx::Value value;
    if (a_menu->uiMovie->GetVariable(&value, VDSG_NUMBER_PATH)) {
        if (value.IsString()) {
            return value.GetString();
        }
    }

    return "";
}

std::string LoadingMenuHook::GetPlayerLevel(RE::LoadingMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return "";
    }

    Scaleform::GFx::Value value;
    if (a_menu->uiMovie->GetVariable(&value, PLAYER_LEVEL_PATH)) {
        if (value.IsString()) {
            return value.GetString();
        }
    }

    return "";
}

bool LoadingMenuHook::IsMinimalMode(RE::LoadingMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return false;  // Default to non-minimal if we can't check
    }

    Scaleform::GFx::Value value;
    if (a_menu->uiMovie->GetVariable(&value, MINIMAL_MODE_PATH)) {
        if (value.IsBoolean()) {
            return value.GetBoolean();
        }
    }

    return false;
}

std::int32_t LoadingMenuHook::GetXPProgressPercent()
{
    auto* saveManager = RE::BGSSaveLoadManager::GetSingleton();
    if (!saveManager || !saveManager->queuedEntryToLoad) {
        return -1;
    }

    auto* entry = saveManager->queuedEntryToLoad;
    // levelProgress = current XP toward next level
    // levelThreshold = total XP needed for next level
    if (entry->levelThreshold <= 0.0f) {
        return -1;
    }

    float percent = (entry->levelProgress / entry->levelThreshold) * 100.0f;
    return static_cast<std::int32_t>(percent);
}
