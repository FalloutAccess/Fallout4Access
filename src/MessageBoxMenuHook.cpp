#include "MessageBoxMenuHook.h"
#include "SpeechManager.h"
#include "LocalizationHelper.h"

MessageBoxMenuHook* MessageBoxMenuHook::GetSingleton()
{
    static MessageBoxMenuHook singleton;
    return &singleton;
}

void MessageBoxMenuHook::Install()
{
    if (m_installed) {
        return;
    }

    // Hook MessageBoxMenu::AdvanceMovie (vtable index 4)
    static REL::HookVFT<void(RE::MessageBoxMenu*, float, std::uint64_t)> hook(
        VTABLE::MessageBoxMenu[0],
        4,  // AdvanceMovie is at index 4
        HookedAdvanceMovie
    );

    if (hook.Enable()) {
        s_advanceMovieHook = &hook;
        m_installed = true;
        REX::INFO("MessageBoxMenuHook: AdvanceMovie hook installed successfully");
    } else {
        REX::ERROR("MessageBoxMenuHook: Failed to install AdvanceMovie hook");
    }
}

void MessageBoxMenuHook::ResetState()
{
    m_lastSelection = -1;
    m_buttonCount = 0;
    m_initialAnnouncementDone = false;
    m_framesSinceOpen = 0;
}

void MessageBoxMenuHook::HookedAdvanceMovie(RE::MessageBoxMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
    // Call original function first to let the game update state
    if (s_advanceMovieHook) {
        (*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
    }

    // Then check for state changes
    auto* hook = GetSingleton();
    if (hook->m_menuOpen) {
        // Track frames for retry logic
        hook->m_framesSinceOpen++;

        // Do initial announcement if not done yet (retry until success or max frames)
        if (!hook->m_initialAnnouncementDone && hook->m_framesSinceOpen <= MAX_RETRY_FRAMES) {
            hook->AnnounceMessageBox();
        }
        // Check for selection changes
        hook->CheckSelectionChange(a_this);
    }
}

void MessageBoxMenuHook::AnnounceMessageBox()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) {
        return;
    }

    auto menu = ui->GetMenu("MessageBoxMenu"sv);
    if (!menu || !menu->uiMovie) {
        return;
    }

    // Get body text from Scaleform
    std::string bodyText = GetBodyText(menu.get());

    // Get button count from Scaleform
    m_buttonCount = GetButtonCount(menu.get());

    // Get selected index
    std::int32_t selectedIndex = GetSelectedIndex(menu.get());

    // Build announcement: body text, then button count, then selected button
    std::string announcement;

    if (!bodyText.empty()) {
        announcement = LocalizationHelper::CleanString(bodyText);
    }

    if (m_buttonCount > 0) {
        if (!announcement.empty()) {
            announcement += ". ";
        }
        std::string buttonWord = (m_buttonCount == 1)
            ? LocalizationHelper::Translate("$F4A_button")
            : LocalizationHelper::Translate("$F4A_buttons");
        announcement += std::to_string(m_buttonCount) + " " + buttonWord;

        // Announce selected button
        if (selectedIndex >= 0) {
            std::string buttonText = GetButtonText(menu.get(), selectedIndex);
            if (!buttonText.empty()) {
                announcement += ". " + LocalizationHelper::CleanString(buttonText);
                m_lastSelection = selectedIndex;
            }
        }
    }

    if (!announcement.empty()) {
        SpeakText(announcement);
        m_initialAnnouncementDone = true;
    } else if (m_framesSinceOpen >= MAX_RETRY_FRAMES) {
        m_initialAnnouncementDone = true;  // Stop retrying
    }
}

void MessageBoxMenuHook::CheckSelectionChange(RE::MessageBoxMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return;

    std::int32_t currentSelection = GetSelectedIndex(a_menu);

    if (currentSelection >= 0 && currentSelection != m_lastSelection) {
        m_lastSelection = currentSelection;

        std::string buttonText = GetButtonText(a_menu, currentSelection);
        if (!buttonText.empty()) {
            SpeakText(LocalizationHelper::CleanString(buttonText));
        }
    }
}

std::string MessageBoxMenuHook::GetBodyText(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return "";
    }

    Scaleform::GFx::Value value;
    if (a_menu->uiMovie->GetVariable(&value, BODY_TEXT_PATH)) {
        if (value.IsString()) {
            return value.GetString();
        }
    }

    return "";
}

std::int32_t MessageBoxMenuHook::GetButtonCount(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return 0;
    }

    std::string entryListPath = std::string(LIST_PATH) + ".entryList";
    Scaleform::GFx::Value entryList;
    if (a_menu->uiMovie->GetVariable(&entryList, entryListPath.c_str())) {
        if (entryList.IsArray()) {
            return static_cast<std::int32_t>(entryList.GetArraySize());
        }
    }

    return 0;
}

std::int32_t MessageBoxMenuHook::GetSelectedIndex(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return -1;
    }

    std::string indexPath = std::string(LIST_PATH) + ".selectedIndex";
    Scaleform::GFx::Value indexValue;
    if (a_menu->uiMovie->GetVariable(&indexValue, indexPath.c_str())) {
        if (indexValue.IsInt()) {
            return indexValue.GetInt();
        } else if (indexValue.IsNumber()) {
            return static_cast<std::int32_t>(indexValue.GetNumber());
        } else if (indexValue.IsUInt()) {
            return static_cast<std::int32_t>(indexValue.GetUInt());
        }
    }

    return -1;
}

std::string MessageBoxMenuHook::GetButtonText(RE::IMenu* a_menu, std::int32_t a_index)
{
    if (!a_menu || !a_menu->uiMovie || a_index < 0) {
        return "";
    }

    std::string entryListPath = std::string(LIST_PATH) + ".entryList";
    Scaleform::GFx::Value entryList;
    if (!a_menu->uiMovie->GetVariable(&entryList, entryListPath.c_str()) || !entryList.IsArray()) {
        return "";
    }

    if (static_cast<std::uint32_t>(a_index) >= entryList.GetArraySize()) {
        return "";
    }

    Scaleform::GFx::Value entry;
    if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
        return "";
    }

    Scaleform::GFx::Value textValue;
    if (entry.GetMember("text", &textValue) && textValue.IsString()) {
        return textValue.GetString();
    }

    return "";
}

void MessageBoxMenuHook::SpeakText(const std::string& a_text)
{
    SpeechManager::GetSingleton()->Speak(a_text);
}
