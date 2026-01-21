#include "HUDMenuHook.h"
#include "SpeechManager.h"
#include "LocalizationHelper.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#undef ERROR

#include <SRAL.h>
#include <RE/H/HUDMenu.h>
#include <RE/F/FavoritesManager.h>
#include <RE/M/MenuControls.h>
#include <RE/RTTI.h>
#include <F4SE/F4SE.h>
#include <typeinfo>

HUDMenuHook* HUDMenuHook::GetSingleton()
{
    static HUDMenuHook singleton;
    return &singleton;
}

void HUDMenuHook::Install()
{
    if (m_installed) {
        REX::DEBUG("HUDMenuHook: Already installed");
        return;
    }

    // Hook HUDMenu::AdvanceMovie (vtable index 4)
    // VTABLE::HUDMenu[0] is the primary vtable (IMenu/GameMenuBase)
    static REL::HookVFT<void(RE::HUDMenu*, float, std::uint64_t)> hook(
        VTABLE::HUDMenu[0],
        4,  // AdvanceMovie is at index 4
        HookedAdvanceMovie
    );

    if (hook.Enable()) {
        s_advanceMovieHook = &hook;
        REX::INFO("HUDMenuHook: AdvanceMovie hook installed successfully");
    } else {
        REX::ERROR("HUDMenuHook: Failed to install AdvanceMovie hook");
    }

    // Hook HUDMenu::OnButtonEvent (BSInputEventUser vtable index 8)
    // VTABLE::HUDMenu[1] is the BSInputEventUser vtable
    static REL::HookVFT<void(RE::HUDMenu*, const RE::ButtonEvent*)> buttonHook(
        VTABLE::HUDMenu[1],
        8,  // OnButtonEvent is at index 8
        HookedOnButtonEvent
    );

    if (buttonHook.Enable()) {
        s_buttonEventHook = &buttonHook;
        REX::INFO("HUDMenuHook: OnButtonEvent hook installed successfully");
    } else {
        REX::ERROR("HUDMenuHook: Failed to install OnButtonEvent hook");
    }

    m_installed = true;
}

void HUDMenuHook::Uninstall()
{
    if (!m_installed || !s_advanceMovieHook) {
        return;
    }

    if (s_advanceMovieHook->Disable()) {
        m_installed = false;
        REX::INFO("HUDMenuHook: Hook uninstalled");
    }
}

void HUDMenuHook::ResetState()
{
    m_lastMessageCount = 0;
    m_lastObjectiveCount = 0;
    m_lastQuestUpdate.clear();
    m_lastXPValue.clear();
    m_pendingXPValue.clear();
    m_xpValueStableTime = 0;
    m_xpAnnounced = false;
    m_lastTutorialText.clear();
    m_lastLevelUpText.clear();
    m_pendingLevelUpText.clear();
    m_levelUpStableTime = 0;
    m_levelUpAnnounced = false;
    m_lastSubtitleText.clear();
    m_quickLootVisible = false;
    m_lastQuickLootIndex = -1;
    m_lastQuickLootContainer.clear();
    m_lastQuickLootItemName.clear();
    m_quickLootOpeningDone = false;
    m_announcementHistory.clear();
}

void HUDMenuHook::HookedAdvanceMovie(RE::HUDMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
    // Call original function first to let the game update state
    if (s_advanceMovieHook) {
        (*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
    }

    // Then check for notification changes
    auto* hook = GetSingleton();
    if (hook->m_hudOpen) {
        // Check notifications regardless of menu mode - HUD notifications
        // can appear even when other menus are open
        hook->CheckNotifications(a_this);
    }
}

void HUDMenuHook::CheckNotifications(RE::HUDMenu* a_menu)
{
    if (!a_menu) return;

    CheckMessages(a_menu);
    CheckQuestUpdates(a_menu);
    CheckObjectiveUpdates(a_menu);
    CheckXPUpdates(a_menu);
    CheckTutorialText(a_menu);
    CheckLevelUp(a_menu);
    CheckSubtitles(a_menu);
    CheckQuickLoot(a_menu);

    // Clean old entries from deduplication cache periodically
    CleanExpiredAnnouncements();
}

void HUDMenuHook::CheckMessages(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return;
    }

    // Get the Messages_mc object
    Scaleform::GFx::Value messagesClip;
    if (!a_menu->uiMovie->GetVariable(&messagesClip, MESSAGES_MC_PATH)) {
        return;
    }
    if (!messagesClip.IsObject()) {
        return;
    }

    // Get ShownMessageArray - this is a Vector.<HUDMessageItem>
    Scaleform::GFx::Value shownArray;
    if (!messagesClip.GetMember("ShownMessageArray", &shownArray)) {
        return;
    }

    // Get the count/length
    std::uint32_t count = 0;
    if (shownArray.IsArray()) {
        count = shownArray.GetArraySize();
    } else if (shownArray.IsObject()) {
        Scaleform::GFx::Value lengthVal;
        if (shownArray.GetMember("length", &lengthVal)) {
            if (lengthVal.IsNumber()) {
                count = static_cast<std::uint32_t>(lengthVal.GetNumber());
            } else if (lengthVal.IsInt()) {
                count = static_cast<std::uint32_t>(lengthVal.GetInt());
            }
        }
    }

    // Handle array reset (count < last count means array was cleared)
    if (count < m_lastMessageCount) {
        m_lastMessageCount = 0;
    }

    // Also check MessageArray (the queue) for pending messages
    Scaleform::GFx::Value messageQueue;
    std::uint32_t queueCount = 0;
    if (messagesClip.GetMember("MessageArray", &messageQueue)) {
        if (messageQueue.IsArray()) {
            queueCount = messageQueue.GetArraySize();
        } else if (messageQueue.IsObject()) {
            Scaleform::GFx::Value lengthVal;
            if (messageQueue.GetMember("length", &lengthVal)) {
                if (lengthVal.IsNumber()) {
                    queueCount = static_cast<std::uint32_t>(lengthVal.GetNumber());
                } else if (lengthVal.IsInt()) {
                    queueCount = static_cast<std::uint32_t>(lengthVal.GetInt());
                }
            }
        }
    }

    // Check shown messages for new entries
    if (count > m_lastMessageCount) {
        for (std::uint32_t i = m_lastMessageCount; i < count; ++i) {
            Scaleform::GFx::Value item;
            if (!shownArray.GetElement(i, &item) || !item.IsObject()) {
                continue;
            }

            std::string messageText;

            // Method 1: Try reading the text field directly
            // Path: HUDMessageItemInternal_mc.MessageText_tf.text
            Scaleform::GFx::Value internalClip;
            if (item.GetMember("HUDMessageItemInternal_mc", &internalClip) && internalClip.IsObject()) {
                Scaleform::GFx::Value textField;
                if (internalClip.GetMember("MessageText_tf", &textField) && textField.IsObject()) {
                    Scaleform::GFx::Value textVal;
                    if (textField.GetMember("text", &textVal) && textVal.IsString()) {
                        messageText = textVal.GetString();
                    }
                }
            }

            // Method 2: Fallback to data.messageText property
            if (messageText.empty()) {
                Scaleform::GFx::Value dataVal;
                if (item.GetMember("data", &dataVal) && dataVal.IsObject()) {
                    Scaleform::GFx::Value textVal;
                    if (dataVal.GetMember("messageText", &textVal) && textVal.IsString()) {
                        messageText = textVal.GetString();
                    }
                }
            }

            // Announce if we got valid text
            if (!messageText.empty() && ShouldAnnounce(messageText)) {
                REX::INFO("HUD Message: {}", messageText);
                RecordAnnouncement(messageText);
                SpeakText(messageText);
            }
        }
    }

    // Also check the queue for messages we might want to announce early
    // (The queue contains messages waiting to be displayed)
    for (std::uint32_t i = 0; i < queueCount; ++i) {
        Scaleform::GFx::Value item;
        if (!messageQueue.GetElement(i, &item) || !item.IsObject()) {
            continue;
        }

        Scaleform::GFx::Value textVal;
        if (item.GetMember("messageText", &textVal) && textVal.IsString()) {
            std::string messageText = textVal.GetString();
            if (!messageText.empty() && ShouldAnnounce(messageText)) {
                REX::INFO("HUD Message (queued): {}", messageText);
                RecordAnnouncement(messageText);
                SpeakText(messageText);
            }
        }
    }

    m_lastMessageCount = count;
}

void HUDMenuHook::CheckQuestUpdates(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return;
    }

    Scaleform::GFx::Value questName, updateType;

    bool hasName = a_menu->uiMovie->GetVariable(&questName, QUEST_NAME_PATH) && questName.IsString();
    bool hasType = a_menu->uiMovie->GetVariable(&updateType, QUEST_TYPE_PATH) && updateType.IsString();

    if (!hasName || !hasType) {
        return;
    }

    std::string name = questName.GetString();
    std::string type = updateType.GetString();

    // Both empty or whitespace-only means no active quest notification
    auto isBlank = [](const std::string& s) {
        return s.empty() || std::all_of(s.begin(), s.end(), [](char c) { return std::isspace(c); });
    };

    if (isBlank(name) && isBlank(type)) {
        m_lastQuestUpdate.clear();
        return;
    }

    // Build combined announcement
    std::string announcement;
    if (!type.empty() && !name.empty()) {
        announcement = type + ": " + name;
    } else if (!name.empty()) {
        announcement = name;
    } else {
        announcement = type;
    }

    // Check if this is a new/different update
    if (!announcement.empty() && announcement != m_lastQuestUpdate) {
        m_lastQuestUpdate = announcement;
        if (ShouldAnnounce(announcement)) {
            REX::INFO("Quest Update: {}", announcement);
            RecordAnnouncement(announcement);
            SpeakText(announcement);
        }
    }
}

void HUDMenuHook::CheckObjectiveUpdates(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return;
    }

    // Get the ObjectiveUpdates_mc object
    Scaleform::GFx::Value objectivesClip;
    if (!a_menu->uiMovie->GetVariable(&objectivesClip, OBJECTIVES_MC_PATH) || !objectivesClip.IsObject()) {
        return;
    }

    // Get ShownObjectivesV - this is a Vector.<HUDObjectiveItem>
    Scaleform::GFx::Value shownArray;
    if (!objectivesClip.GetMember("ShownObjectivesV", &shownArray)) {
        return;
    }

    // Get the count/length
    std::uint32_t count = 0;
    if (shownArray.IsArray()) {
        count = shownArray.GetArraySize();
    } else if (shownArray.IsObject()) {
        Scaleform::GFx::Value lengthVal;
        if (shownArray.GetMember("length", &lengthVal)) {
            if (lengthVal.IsNumber()) {
                count = static_cast<std::uint32_t>(lengthVal.GetNumber());
            } else if (lengthVal.IsInt()) {
                count = static_cast<std::uint32_t>(lengthVal.GetInt());
            }
        }
    }

    // Handle array reset
    if (count < m_lastObjectiveCount) {
        m_lastObjectiveCount = 0;
    }

    // Check for new objectives
    if (count > m_lastObjectiveCount) {
        for (std::uint32_t i = m_lastObjectiveCount; i < count; ++i) {
            Scaleform::GFx::Value item;
            if (!shownArray.GetElement(i, &item) || !item.IsObject()) {
                continue;
            }

            // Get the data property
            Scaleform::GFx::Value dataVal;
            if (!item.GetMember("data", &dataVal) || !dataVal.IsObject()) {
                continue;
            }

            // Try to get ObjectiveMessage (via getter)
            Scaleform::GFx::Value msgVal;
            std::string objectiveText;

            // The ActionScript has a getter ObjectiveMessage that returns _objectiveMessage
            // Scaleform should expose getters, so this should work
            if (dataVal.GetMember("ObjectiveMessage", &msgVal) && msgVal.IsString()) {
                objectiveText = msgVal.GetString();
            }

            if (objectiveText.empty()) {
                continue;
            }

            // Check if completed
            Scaleform::GFx::Value completedVal;
            bool isCompleted = false;
            if (dataVal.GetMember("isCompleted", &completedVal) && completedVal.IsBoolean()) {
                isCompleted = completedVal.GetBoolean();
            }

            std::string announcement = isCompleted ? "Completed: " + objectiveText : objectiveText;

            if (ShouldAnnounce(announcement)) {
                REX::INFO("Objective: {}", announcement);
                RecordAnnouncement(announcement);
                SpeakText(announcement);
            }
        }
    }

    m_lastObjectiveCount = count;
}

void HUDMenuHook::CheckXPUpdates(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return;
    }

    Scaleform::GFx::Value xpValue;
    if (!a_menu->uiMovie->GetVariable(&xpValue, XP_NUMBER_PATH) || !xpValue.IsString()) {
        return;
    }

    std::string xpText = xpValue.GetString();
    auto now = GetCurrentTimeMs();

    // XP text is empty when meter is hidden - reset state
    if (xpText.empty()) {
        m_lastXPValue.clear();
        m_pendingXPValue.clear();
        m_xpAnnounced = false;
        return;
    }

    // Try to parse as number and skip if zero
    int xpNum = 0;
    try {
        xpNum = std::stoi(xpText);
        if (xpNum <= 0) {
            m_lastXPValue.clear();
            m_pendingXPValue.clear();
            m_xpAnnounced = false;
            return;
        }
    } catch (...) {
        return;
    }

    // XP meter animates the number counting up - wait for it to stabilize
    if (xpText != m_pendingXPValue) {
        m_pendingXPValue = xpText;
        m_xpValueStableTime = now;
        m_xpAnnounced = false;
    } else if (!m_xpAnnounced && (now - m_xpValueStableTime) >= kXPStabilizeMs) {
        m_xpAnnounced = true;
        m_lastXPValue = xpText;

        std::string announcement = "+" + std::to_string(xpNum) + " XP";

        if (ShouldAnnounce(announcement)) {
            REX::INFO("XP: {}", announcement);
            RecordAnnouncement(announcement);
            SpeakText(announcement);
        }
    }
}

void HUDMenuHook::CheckTutorialText(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return;
    }

    Scaleform::GFx::Value tutorialValue;
    if (!a_menu->uiMovie->GetVariable(&tutorialValue, TUTORIAL_TEXT_PATH) || !tutorialValue.IsString()) {
        return;
    }

    std::string tutorialText = tutorialValue.GetString();

    // Empty means no tutorial showing
    if (tutorialText.empty()) {
        m_lastTutorialText.clear();
        return;
    }

    // Filter out meta-tutorials that describe UI layout (not useful for blind users)
    if (tutorialText.find("tutorials appear") != std::string::npos ||
        tutorialText.find("in this box") != std::string::npos ||
        tutorialText.find("quest VB") != std::string::npos) {
        return;
    }

    // Announce if different from last
    if (tutorialText != m_lastTutorialText) {
        m_lastTutorialText = tutorialText;
        if (ShouldAnnounce(tutorialText)) {
            REX::INFO("Tutorial: {}", tutorialText);
            RecordAnnouncement(tutorialText);
            SpeakText(tutorialText);
        }
    }
}

void HUDMenuHook::CheckLevelUp(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return;
    }

    Scaleform::GFx::Value levelUpValue;
    if (!a_menu->uiMovie->GetVariable(&levelUpValue, LEVELUP_TEXT_PATH) || !levelUpValue.IsString()) {
        return;
    }

    std::string levelUpText = levelUpValue.GetString();
    auto now = GetCurrentTimeMs();

    // Empty means no level up showing - reset state
    if (levelUpText.empty()) {
        m_lastLevelUpText.clear();
        m_pendingLevelUpText.clear();
        m_levelUpAnnounced = false;
        return;
    }

    // Level up text animates letter by letter - wait for it to stabilize
    if (levelUpText != m_pendingLevelUpText) {
        m_pendingLevelUpText = levelUpText;
        m_levelUpStableTime = now;
        m_levelUpAnnounced = false;
    } else if (!m_levelUpAnnounced && (now - m_levelUpStableTime) >= kLevelUpStabilizeMs) {
        m_levelUpAnnounced = true;
        m_lastLevelUpText = levelUpText;

        if (ShouldAnnounce(levelUpText)) {
            REX::INFO("Level Up: {}", levelUpText);
            RecordAnnouncement(levelUpText);
            SpeakText(levelUpText);
        }
    }
}

void HUDMenuHook::CheckSubtitles(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return;
    }

    // Get subtitle text
    Scaleform::GFx::Value subtitleValue;
    if (!a_menu->uiMovie->GetVariable(&subtitleValue, SUBTITLE_TEXT_PATH) || !subtitleValue.IsString()) {
        // No subtitle showing - reset tracking
        if (!m_lastSubtitleText.empty()) {
            m_lastSubtitleText.clear();
        }
        return;
    }

    std::string subtitleText = subtitleValue.GetString();

    // Empty subtitle means nothing is showing
    if (subtitleText.empty()) {
        m_lastSubtitleText.clear();
        return;
    }

    // Check if this is a new subtitle (different from last)
    if (subtitleText == m_lastSubtitleText) {
        return;
    }

    m_lastSubtitleText = subtitleText;

    // Get speaker name (optional)
    std::string speakerName;
    Scaleform::GFx::Value speakerValue;
    if (a_menu->uiMovie->GetVariable(&speakerValue, SPEAKER_NAME_PATH) && speakerValue.IsString()) {
        speakerName = speakerValue.GetString();
    }

    // Build announcement with speaker name if available
    std::string announcement;
    if (!speakerName.empty()) {
        announcement = speakerName + ": " + subtitleText;
    } else {
        announcement = subtitleText;
    }

    REX::INFO("Subtitle: {}", announcement);
    SpeakText(announcement);
}

std::uint64_t HUDMenuHook::GetCurrentTimeMs() const
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

bool HUDMenuHook::ShouldAnnounce(const std::string& a_text)
{
    if (a_text.empty()) {
        return false;
    }

    auto it = m_announcementHistory.find(a_text);
    if (it == m_announcementHistory.end()) {
        return true;
    }

    auto now = GetCurrentTimeMs();
    return (now - it->second) >= kDeduplicationWindowMs;
}

void HUDMenuHook::RecordAnnouncement(const std::string& a_text)
{
    m_announcementHistory[a_text] = GetCurrentTimeMs();
}

void HUDMenuHook::CleanExpiredAnnouncements()
{
    // Only clean periodically, not every frame
    static std::uint64_t lastCleanTime = 0;
    auto now = GetCurrentTimeMs();

    if (now - lastCleanTime < 5000) {  // Clean every 5 seconds max
        return;
    }
    lastCleanTime = now;

    for (auto it = m_announcementHistory.begin(); it != m_announcementHistory.end();) {
        if ((now - it->second) > kDeduplicationWindowMs * 2) {
            it = m_announcementHistory.erase(it);
        } else {
            ++it;
        }
    }
}

void HUDMenuHook::SpeakText(const std::string& a_text)
{
    SpeechManager::GetSingleton()->Speak(a_text);
}

// ============================================================================
// Quick Loot (QuickContainerWidget) Accessibility
// ============================================================================

void HUDMenuHook::CheckQuickLoot(RE::HUDMenu* a_menu)
{
    if (!a_menu) return;

    bool currentlyVisible = IsQuickLootVisible(a_menu);

    // Visibility state change: widget appeared
    if (currentlyVisible && !m_quickLootVisible) {
        m_quickLootVisible = true;
        m_quickLootOpeningDone = false;
        m_lastQuickLootIndex = -1;

        // Announce container + first item (1-frame deferred for Scaleform population)
        if (auto* taskInterface = F4SE::GetTaskInterface()) {
            taskInterface->AddTask([this]() {
                if (!m_quickLootVisible) return;
                auto ui = RE::UI::GetSingleton();
                if (!ui) return;
                auto menu = ui->GetMenu("HUDMenu"sv);
                if (!menu) return;
                auto* hudMenu = static_cast<RE::HUDMenu*>(menu.get());
                AnnounceQuickLootOpened(hudMenu);
            });
        }
    }
    // Visibility state change: widget disappeared
    else if (!currentlyVisible && m_quickLootVisible) {
        m_quickLootVisible = false;
        m_quickLootOpeningDone = false;
        // Don't clear m_lastQuickLootContainer - prevents re-announcing same container
        // Don't announce closing - would be too chatty as player looks around

        // Clear cached DataModel pointer (will be different for next container)
        m_hudQuickContainerDataModelPtr = nullptr;
        m_hudQuickContainerPtr = nullptr;
    }

    // If visible and opening announcement done, check for item selection changes
    if (m_quickLootVisible && m_quickLootOpeningDone) {
        std::int32_t currentIndex = GetQuickLootSelectedIndex(a_menu);

        if (currentIndex >= 0) {
            auto item = GetQuickLootItemAtIndex(a_menu, currentIndex);

            // Announce if index changed OR if item at same index changed (after taking item)
            bool indexChanged = (currentIndex != m_lastQuickLootIndex);
            bool itemChanged = item.IsValid() && (item.name != m_lastQuickLootItemName);

            if (indexChanged || itemChanged) {
                m_lastQuickLootIndex = currentIndex;
                m_lastQuickLootItemName = item.name;

                if (item.IsValid()) {
                    AnnounceQuickLootItem(item);
                }
            }
        }
    }
}

bool HUDMenuHook::IsQuickLootVisible(RE::HUDMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return false;

    Scaleform::GFx::Value widget;
    if (!a_menu->uiMovie->GetVariable(&widget, QUICKLOOT_WIDGET_PATH)) {
        return false;
    }

    if (!widget.IsObject()) return false;

    // Check if widget is visible
    Scaleform::GFx::Value visibleProp;
    if (widget.GetMember("visible", &visibleProp) && visibleProp.IsBoolean()) {
        if (!visibleProp.GetBoolean()) return false;
    }

    // Also check alpha (widget fades in/out)
    Scaleform::GFx::Value alphaProp;
    if (widget.GetMember("alpha", &alphaProp)) {
        double alpha = 0.0;
        if (alphaProp.IsNumber()) {
            alpha = alphaProp.GetNumber();
        } else if (alphaProp.IsInt()) {
            alpha = static_cast<double>(alphaProp.GetInt());
        }
        if (alpha < 0.5) return false;  // Wait until more visible
    }

    // Check if first item clip is visible (ListItems_mc.ItemText0)
    // This is more reliable than checking ItemDataA Vector
    Scaleform::GFx::Value listItems;
    if (!widget.GetMember("ListItems_mc", &listItems) || !listItems.IsObject()) {
        return false;
    }

    Scaleform::GFx::Value itemClip;
    if (!listItems.GetMember("ItemText0", &itemClip) || !itemClip.IsObject()) {
        return false;
    }

    // Check if item clip is visible (means it has data)
    Scaleform::GFx::Value clipVisible;
    if (itemClip.GetMember("visible", &clipVisible) && clipVisible.IsBoolean()) {
        return clipVisible.GetBoolean();
    }

    return false;
}

std::int32_t HUDMenuHook::GetQuickLootSelectedIndex(RE::HUDMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return -1;

    Scaleform::GFx::Value widget;
    if (!a_menu->uiMovie->GetVariable(&widget, QUICKLOOT_WIDGET_PATH) || !widget.IsObject()) {
        return -1;
    }

    // Try _selectedIndex first (may be accessible)
    Scaleform::GFx::Value indexValue;
    if (widget.GetMember("_selectedIndex", &indexValue)) {
        if (indexValue.IsInt()) {
            return indexValue.GetInt();
        } else if (indexValue.IsNumber()) {
            return static_cast<std::int32_t>(indexValue.GetNumber());
        }
    }

    // Fallback: find selected item by checking each clip's selected property
    Scaleform::GFx::Value listItems;
    if (!widget.GetMember("ListItems_mc", &listItems) || !listItems.IsObject()) {
        return 0;  // Default to first item
    }

    for (int i = 0; i < 5; ++i) {
        std::string clipName = "ItemText" + std::to_string(i);
        Scaleform::GFx::Value itemClip;
        if (!listItems.GetMember(clipName.c_str(), &itemClip) || !itemClip.IsObject()) {
            continue;
        }

        // Check if this clip is selected
        Scaleform::GFx::Value selectedProp;
        if (itemClip.GetMember("selected", &selectedProp) && selectedProp.IsBoolean()) {
            if (selectedProp.GetBoolean()) {
                return i;
            }
        }

        // Also try _selected
        if (itemClip.GetMember("_selected", &selectedProp) && selectedProp.IsBoolean()) {
            if (selectedProp.GetBoolean()) {
                return i;
            }
        }
    }

    return 0;  // Default to first item if none found selected
}

void HUDMenuHook::SetQuickLootSelectedIndex(RE::HUDMenu* a_menu, std::int32_t a_index)
{
    if (!a_menu || !a_menu->uiMovie || a_index < 0) return;

    Scaleform::GFx::Value widget;
    if (!a_menu->uiMovie->GetVariable(&widget, QUICKLOOT_WIDGET_PATH) || !widget.IsObject()) {
        return;
    }

    // Set _selectedIndex on the widget (Value constructor takes int32_t)
    Scaleform::GFx::Value indexValue(a_index);
    widget.SetMember("_selectedIndex", indexValue);

    // Call InvalidateData to force UI update
    Scaleform::GFx::Value result;
    widget.Invoke("InvalidateData", &result, nullptr, 0);
}

ItemIdentity HUDMenuHook::GetQuickLootItemAtIndex(RE::HUDMenu* a_menu, std::int32_t a_index)
{
    ItemIdentity item;
    if (!a_menu || !a_menu->uiMovie || a_index < 0 || a_index >= 5) return item;

    Scaleform::GFx::Value widget;
    if (!a_menu->uiMovie->GetVariable(&widget, QUICKLOOT_WIDGET_PATH) || !widget.IsObject()) {
        return item;
    }

    // Get ListItems_mc
    Scaleform::GFx::Value listItems;
    if (!widget.GetMember("ListItems_mc", &listItems) || !listItems.IsObject()) {
        return item;
    }

    // Get the item clip (ItemText0, ItemText1, etc.)
    std::string clipName = "ItemText" + std::to_string(a_index);
    Scaleform::GFx::Value itemClip;
    if (!listItems.GetMember(clipName.c_str(), &itemClip) || !itemClip.IsObject()) {
        return item;
    }

    // Check if clip is visible (has data)
    Scaleform::GFx::Value clipVisible;
    if (itemClip.GetMember("visible", &clipVisible) && clipVisible.IsBoolean()) {
        if (!clipVisible.GetBoolean()) return item;
    }

    // Get the data property (QuickContainerItemData)
    Scaleform::GFx::Value data;
    if (!itemClip.GetMember("data", &data) || !data.IsObject()) {
        // Fallback: try reading directly from clip's _data
        if (!itemClip.GetMember("_data", &data) || !data.IsObject()) {
            return item;
        }
    }

    // Extract item properties from data
    Scaleform::GFx::Value prop;

    // Name (text)
    if (data.GetMember("text", &prop) && prop.IsString()) {
        item.name = LocalizationHelper::CleanString(prop.GetString());
    }

    // Count
    if (data.GetMember("count", &prop)) {
        if (prop.IsInt()) {
            item.count = prop.GetInt();
        } else if (prop.IsNumber()) {
            item.count = static_cast<std::int32_t>(prop.GetNumber());
        } else if (prop.IsUInt()) {
            item.count = static_cast<std::int32_t>(prop.GetUInt());
        }
    }

    // Equip state
    if (data.GetMember("equipState", &prop)) {
        if (prop.IsInt()) {
            item.equipState = prop.GetInt();
        } else if (prop.IsNumber()) {
            item.equipState = static_cast<std::int32_t>(prop.GetNumber());
        } else if (prop.IsUInt()) {
            item.equipState = static_cast<std::int32_t>(prop.GetUInt());
        }
    }

    // Legendary
    if (data.GetMember("isLegendary", &prop) && prop.IsBoolean()) {
        item.isLegendary = prop.GetBoolean();
    }

    // Favorite
    if (data.GetMember("favorite", &prop)) {
        if (prop.IsBoolean()) {
            item.isFavorite = prop.GetBoolean();
        } else if (prop.IsInt() || prop.IsNumber() || prop.IsUInt()) {
            int favVal = 0;
            if (prop.IsInt()) favVal = prop.GetInt();
            else if (prop.IsNumber()) favVal = static_cast<int>(prop.GetNumber());
            else if (prop.IsUInt()) favVal = static_cast<int>(prop.GetUInt());
            item.isFavorite = (favVal > 0);
        }
    }

    // Tagged for search
    if (data.GetMember("taggedForSearch", &prop) && prop.IsBoolean()) {
        item.isTagged = prop.GetBoolean();
    }

    return item;
}

std::string HUDMenuHook::GetQuickLootContainerName(RE::HUDMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    Scaleform::GFx::Value widget;
    if (!a_menu->uiMovie->GetVariable(&widget, QUICKLOOT_WIDGET_PATH) || !widget.IsObject()) {
        return "";
    }

    // Navigate: ListHeaderAndBracket_mc -> ContainerName_mc -> textField_tf -> text
    Scaleform::GFx::Value headerHolder;
    if (!widget.GetMember("ListHeaderAndBracket_mc", &headerHolder) || !headerHolder.IsObject()) {
        return "";
    }

    Scaleform::GFx::Value containerName;
    if (!headerHolder.GetMember("ContainerName_mc", &containerName) || !containerName.IsObject()) {
        return "";
    }

    Scaleform::GFx::Value textField;
    if (!containerName.GetMember("textField_tf", &textField) || !textField.IsObject()) {
        return "";
    }

    Scaleform::GFx::Value textValue;
    if (textField.GetMember("text", &textValue) && textValue.IsString()) {
        return LocalizationHelper::CleanString(textValue.GetString());
    }

    return "";
}

void HUDMenuHook::AnnounceQuickLootOpened(RE::HUDMenu* a_menu)
{
    // Don't announce container name - crosshair already announces it
    // Just announce the first item
    std::int32_t index = GetQuickLootSelectedIndex(a_menu);
    if (index >= 0) {
        auto item = GetQuickLootItemAtIndex(a_menu, index);
        if (item.IsValid()) {
            AnnounceQuickLootItem(item);
            m_lastQuickLootIndex = index;
            m_lastQuickLootItemName = item.name;
        }
    } else {
        // If no selected index, default to first item (index 0)
        auto item = GetQuickLootItemAtIndex(a_menu, 0);
        if (item.IsValid()) {
            AnnounceQuickLootItem(item);
            m_lastQuickLootIndex = 0;
            m_lastQuickLootItemName = item.name;
        }
    }

    m_quickLootOpeningDone = true;
}

void HUDMenuHook::AnnounceQuickLootItem(const ItemIdentity& a_item)
{
    // Use standard item announcement format (name, count, flags)
    std::string announcement = a_item.BuildBasicAnnouncement();

    // Apply deduplication (prevents rapid re-announcements on index glitches)
    if (ShouldAnnounce(announcement)) {
        RecordAnnouncement(announcement);
        SpeakText(announcement);
    }
}

// ============================================================================
// Keyboard Input Hook for Quick Loot Arrow Key Navigation
// ============================================================================

void HUDMenuHook::HookedOnButtonEvent(RE::HUDMenu* a_this, const RE::ButtonEvent* a_event)
{
    auto* hook = GetSingleton();

    // Check for arrow keys when Quick Loot is visible
    if (a_event && a_event->QJustPressed() && hook->m_quickLootVisible) {
        auto device = a_event->device.get();
        auto idCode = a_event->QIDCode();

        // Only handle keyboard input
        if (device == RE::INPUT_DEVICE::kKeyboard) {
            // Up arrow (0x26) or Down arrow (0x28)
            if (idCode == 0x26 || idCode == 0x28) {
                // IMPORTANT: a_this is BSInputEventUser* at offset 0x10, NOT HUDMenu*
                // Get the actual HUDMenu from UI system instead
                auto ui = RE::UI::GetSingleton();
                if (ui) {
                    auto menu = ui->GetMenu<RE::HUDMenu>();
                    if (menu) {
                        bool isUp = (idCode == 0x26);
                        hook->HandleQuickLootNavigation(menu.get(), isUp);
                    }
                }
            }
        }
    }

    // Always call original OnButtonEvent (pass a_this as-is for correct vtable call)
    if (s_buttonEventHook) {
        (*s_buttonEventHook)(a_this, a_event);
    }
}

void HUDMenuHook::HandleQuickLootNavigation(RE::HUDMenu* a_menu, bool a_up)
{
    if (!a_menu || !m_quickLootVisible) return;

    // Find the DataModel and call native functions
    // Native functions handle all bounds checking and scrolling internally
    void* dataModel = FindQuickContainerDataModel(a_menu);
    if (!dataModel) {
        REX::ERROR("HUDMenuHook: DataModel not found, cannot navigate");
        m_hudQuickContainerDataModelPtr = nullptr;  // Clear cache to force retry on next attempt
        return;
    }

    if (a_up) {
        CallNativeQuickContainerUp(dataModel);
    } else {
        CallNativeQuickContainerDown(dataModel);
    }

    // Capture current state by value to avoid race conditions with main thread
    std::string previousItemName = m_lastQuickLootItemName;

    // Defer announcement to next frame to allow Scaleform to update
    if (auto* taskInterface = F4SE::GetTaskInterface()) {
        taskInterface->AddTask([this, previousItemName]() {
            if (!m_quickLootVisible) return;

            auto ui = RE::UI::GetSingleton();
            if (!ui) return;

            auto menu = ui->GetMenu<RE::HUDMenu>();
            if (!menu) return;

            // Read the current selection from Scaleform (now updated)
            std::int32_t currentIndex = GetQuickLootSelectedIndex(menu.get());
            if (currentIndex < 0) return;

            auto item = GetQuickLootItemAtIndex(menu.get(), currentIndex);
            if (item.IsValid() && item.name != previousItemName) {
                m_lastQuickLootIndex = currentIndex;
                m_lastQuickLootItemName = item.name;
                AnnounceQuickLootItem(item);
            }
        });
    }
}

std::int32_t HUDMenuHook::GetQuickLootItemCount(RE::HUDMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return 0;

    Scaleform::GFx::Value widget;
    if (!a_menu->uiMovie->GetVariable(&widget, QUICKLOOT_WIDGET_PATH) || !widget.IsObject()) {
        return 0;
    }

    // Get ListItems_mc and count visible item clips
    Scaleform::GFx::Value listItems;
    if (!widget.GetMember("ListItems_mc", &listItems) || !listItems.IsObject()) {
        return 0;
    }

    std::int32_t count = 0;
    for (int i = 0; i < 5; ++i) {
        std::string clipName = "ItemText" + std::to_string(i);
        Scaleform::GFx::Value itemClip;
        if (!listItems.GetMember(clipName.c_str(), &itemClip) || !itemClip.IsObject()) {
            break;
        }

        // Check if clip is visible (has data)
        Scaleform::GFx::Value clipVisible;
        if (itemClip.GetMember("visible", &clipVisible) && clipVisible.IsBoolean()) {
            if (clipVisible.GetBoolean()) {
                count++;
            } else {
                break;  // Items are sequential, stop at first invisible one
            }
        }
    }

    return count;
}

void HUDMenuHook::SetQuickLootSelection(RE::HUDMenu* a_menu, std::int32_t a_index)
{
    if (!a_menu) return;

    std::int32_t currentIndex = GetQuickLootSelectedIndex(a_menu);
    if (currentIndex == a_index) return;  // Already at target

    // Determine direction
    bool goingUp = a_index < currentIndex;
    int steps = std::abs(a_index - currentIndex);

    // Simulate mouse wheel events to navigate
    for (int i = 0; i < steps; ++i) {
        SimulateQuickLootScroll(a_menu, goingUp);
    }
}

void HUDMenuHook::SimulateQuickLootScroll(RE::HUDMenu* a_menu, bool a_up)
{
    if (!a_menu) return;

    // Construct ButtonEvent with ALL required fields properly initialized
    RE::ButtonEvent event;

    // CRITICAL: Fix vtable pointer to use game's vtable, not our DLL's vtable
    // Without this, the game crashes when it tries to use virtual functions/RTTI
    *reinterpret_cast<std::uintptr_t*>(&event) = RE::ButtonEvent::VTABLE[0].address();

    // InputEvent base fields
    event.device = RE::INPUT_DEVICE::kMouse;
    event.deviceID = 0;
    event.eventType = RE::INPUT_EVENT_TYPE::kButton;
    event.next = nullptr;
    event.timeCode = 0;
    event.handled = RE::InputEvent::HANDLED_RESULT::kUnhandled;

    // IDEvent fields
    event.strUserEvent = a_up ? "QuickContainerUp" : "QuickContainerDown";
    event.idCode = a_up ? 0x800 : 0x900;  // kWheelUp / kWheelDown
    event.disabled = false;

    // ButtonEvent fields
    event.value = 1.0f;        // Pressed
    event.heldDownSecs = 0.0f; // Just pressed

    // Call the original OnButtonEvent handler
    // IMPORTANT: The hook is on BSInputEventUser vtable (at offset 0x10 in HUDMenu)
    // We must pass the BSInputEventUser* interface pointer, not HUDMenu*
    if (s_buttonEventHook) {
        auto* inputUser = reinterpret_cast<RE::HUDMenu*>(
            reinterpret_cast<std::uintptr_t>(a_menu) + 0x10
        );
        (*s_buttonEventHook)(inputUser, &event);
    }
}

// ============================================================================
// Debug: Dump Quick Container Input Mappings
// ============================================================================

// Helper to get RTTI type name from a pointer (returns nullptr if invalid)
// Uses IsBadReadPtr to validate memory before dereferencing to avoid crashes
// from third-party mods with non-standard RTTI structures
const char* HUDMenuHook::GetRTTITypeName(void* a_ptr)
{
    if (!a_ptr) return nullptr;

    __try {
        // Validate we can read the vtable pointer
        if (IsBadReadPtr(a_ptr, sizeof(void*))) return nullptr;

        void** vtable = *reinterpret_cast<void***>(a_ptr);
        if (!vtable) return nullptr;

        // Validate we can read vtable[-1] (the COL pointer)
        if (IsBadReadPtr(vtable - 1, sizeof(void*))) return nullptr;

        auto* col = reinterpret_cast<RE::RTTI::CompleteObjectLocator*>(vtable[-1]);
        if (!col) return nullptr;

        // Validate we can read the COL structure
        if (IsBadReadPtr(col, sizeof(RE::RTTI::CompleteObjectLocator))) return nullptr;

        auto* typeDesc = col->typeDescriptor.get();
        if (!typeDesc) return nullptr;

        // Validate we can read the type descriptor
        if (IsBadReadPtr(typeDesc, sizeof(RE::RTTI::TypeDescriptor))) return nullptr;

        // Validate the name pointer (it's embedded in the struct, but check the first char)
        const char* name = typeDesc->name();
        if (IsBadReadPtr(name, 1)) return nullptr;

        return name;
    }
    __except(1) {
        return nullptr;
    }
}

void HUDMenuHook::DebugDumpHUDQuickContainer(RE::HUDMenu* a_menu)
{
    if (!a_menu) {
        REX::INFO("HUDMenu is null");
        return;
    }

    REX::INFO("=== Searching for HUDQuickContainer in hudObjects ===");
    REX::INFO("hudObjects count: {}", static_cast<std::uint32_t>(a_menu->hudObjects.size()));

    for (std::uint32_t i = 0; i < a_menu->hudObjects.size(); ++i) {
        auto& obj = a_menu->hudObjects[i];
        if (!obj) continue;

        // Get raw pointer
        void* rawPtr = obj.get();
        if (!rawPtr) continue;

        const char* typeName = GetRTTITypeName(rawPtr);
        if (!typeName) continue;

        REX::INFO("hudObjects[{}]: type = {}, addr = 0x{:X}", i, typeName, reinterpret_cast<std::uintptr_t>(rawPtr));

        // Check if this is HUDQuickContainer
        if (std::string_view(typeName).find("HUDQuickContainer") != std::string_view::npos &&
            std::string_view(typeName).find("DataModel") == std::string_view::npos) {
            REX::INFO("*** FOUND HUDQuickContainer at index {} ***", i);

            // Store the pointer for later use
            m_hudQuickContainerPtr = rawPtr;
            m_hudQuickContainerIndex = i;

            // Now search for HUDQuickContainerDataModel pointer within HUDQuickContainer
            REX::INFO("Searching for HUDQuickContainerDataModel pointers in HUDQuickContainer...");
            auto* bytes = reinterpret_cast<std::uint8_t*>(rawPtr);

            for (std::uint32_t offset = 0; offset < 0x300; offset += 8) {
                std::uintptr_t potentialPtr = *reinterpret_cast<std::uintptr_t*>(bytes + offset);

                // Check if it looks like a valid pointer (heap range heuristic)
                if (potentialPtr > 0x10000 && potentialPtr < 0x7FFFFFFFFFFF) {
                    const char* pointedTypeName = GetRTTITypeName(reinterpret_cast<void*>(potentialPtr));
                    if (pointedTypeName) {
                        REX::INFO("  Offset 0x{:X}: ptr -> {} (addr 0x{:X})",
                            offset, pointedTypeName, potentialPtr);

                        // Check if this is the DataModel
                        if (std::string_view(pointedTypeName).find("HUDQuickContainerDataModel") != std::string_view::npos) {
                            REX::INFO("  *** FOUND HUDQuickContainerDataModel at offset 0x{:X}! ***", offset);
                            m_hudQuickContainerDataModelPtr = reinterpret_cast<void*>(potentialPtr);
                            m_hudQuickContainerDataModelOffset = offset;

                            // Scan the DataModel for potential selectedClipIndex
                            REX::INFO("  Scanning DataModel for selectedClipIndex (values 0-4):");
                            auto* dmBytes = reinterpret_cast<std::uint8_t*>(potentialPtr);
                            for (std::uint32_t dmOffset = 0; dmOffset < 0x200; dmOffset += 4) {
                                std::int32_t value = *reinterpret_cast<std::int32_t*>(dmBytes + dmOffset);
                                if (value >= 0 && value <= 4) {
                                    REX::INFO("    DataModel offset 0x{:X}: int32 = {}", dmOffset, value);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    REX::INFO("=== End HUDQuickContainer search ===");
}

void HUDMenuHook::DebugDumpQuickContainerMappings()
{
    auto* controlMap = RE::ControlMap::GetSingleton();
    if (!controlMap) {
        REX::INFO("ControlMap not available");
        return;
    }

    REX::INFO("=== Quick Container Input Mappings (Context 0x19) ===");

    // kQuickContainerMenu context ID is 0x19 (25)
    constexpr auto kQuickContainerContext = static_cast<RE::UserEvents::INPUT_CONTEXT_ID>(0x19);

    // Test common event names and see what keys they map to
    const char* testEvents[] = {
        "Up", "Down", "Left", "Right",
        "ScrollUp", "ScrollDown",
        "Previous", "Next",
        "QuickContainerUp", "QuickContainerDown",
        "Accept", "Cancel", "Activate",
        "Select", "Back", "Forward",
        "PrevPage", "NextPage",
        "MoveUp", "MoveDown",
        "XButton", "YButton",
        "RSUp", "RSDown", "LSUp", "LSDown"
    };

    for (const char* eventName : testEvents) {
        auto mouseKey = controlMap->GetMappedKey(
            eventName,
            RE::INPUT_DEVICE::kMouse,
            kQuickContainerContext
        );
        auto kbKey = controlMap->GetMappedKey(
            eventName,
            RE::INPUT_DEVICE::kKeyboard,
            kQuickContainerContext
        );
        auto gamepadKey = controlMap->GetMappedKey(
            eventName,
            RE::INPUT_DEVICE::kGamepad,
            kQuickContainerContext
        );

        // 0xFF means unmapped
        if (mouseKey != 0xFF || kbKey != 0xFF || gamepadKey != 0xFF) {
            REX::INFO("Event '{}': mouse=0x{:X}, keyboard=0x{:X}, gamepad=0x{:X}",
                eventName, mouseKey, kbKey, gamepadKey);
        }
    }

    // Also check kMain context (0x00) for comparison
    REX::INFO("=== Main Context (0x00) for comparison ===");
    constexpr auto kMainContext = static_cast<RE::UserEvents::INPUT_CONTEXT_ID>(0x00);

    for (const char* eventName : testEvents) {
        auto mouseKey = controlMap->GetMappedKey(
            eventName,
            RE::INPUT_DEVICE::kMouse,
            kMainContext
        );
        auto kbKey = controlMap->GetMappedKey(
            eventName,
            RE::INPUT_DEVICE::kKeyboard,
            kMainContext
        );

        if (mouseKey != 0xFF || kbKey != 0xFF) {
            REX::INFO("[Main] Event '{}': mouse=0x{:X}, keyboard=0x{:X}",
                eventName, mouseKey, kbKey);
        }
    }

    // Check mouse wheel codes specifically (0x08 = WheelUp, 0x09 = WheelDown)
    REX::INFO("=== Mouse Wheel Raw Codes ===");
    REX::INFO("Note: Mouse WheelUp=0x08, WheelDown=0x09 (DirectInput)");
    REX::INFO("Note: Game may use 264/265 (0x108/0x109) for scroll events");

    REX::INFO("=== End Quick Container Mappings ===");
}

// ============================================================================
// Native Game Function Calls for Quick Container Navigation
// ============================================================================

void* HUDMenuHook::FindQuickContainerDataModel([[maybe_unused]] RE::HUDMenu* a_menu)
{
    // Helper lambda for exact RTTI type matching (MSVC format: .?AVClassName@@)
    auto isValidRTTI = [](const char* typeName) -> bool {
        if (!typeName) return false;
        std::string_view name(typeName);
        // MSVC RTTI format: ".?AVHUDQuickContainerDataModel@@" or similar with namespaces
        return name.find(".?AVHUDQuickContainerDataModel@") != std::string_view::npos;
    };

    // Return cached pointer if still valid
    if (m_hudQuickContainerDataModelPtr) {
        const char* typeName = GetRTTITypeName(m_hudQuickContainerDataModelPtr);
        if (isValidRTTI(typeName)) {
            return m_hudQuickContainerDataModelPtr;
        }
        m_hudQuickContainerDataModelPtr = nullptr;
    }

    // Access via HUDDataModel singleton (RVA 0x32694E8)
    // HUDQuickContainerDataModel is embedded at offset 0x38 from HUDDataModel
    static REL::Relocation<void**> hudDataModelSingleton{ REL::Offset(0x32694E8) };

    void* hudDataModel = *hudDataModelSingleton;
    if (!hudDataModel) {
        REX::DEBUG("HUDMenuHook: HUDDataModel singleton is null");
        return nullptr;
    }

    // Validate pointer range (basic sanity check for valid heap/module address)
    std::uintptr_t hudDataModelAddr = reinterpret_cast<std::uintptr_t>(hudDataModel);
    if (hudDataModelAddr < 0x10000 || hudDataModelAddr > 0x7FFFFFFFFFFF) {
        REX::DEBUG("HUDMenuHook: HUDDataModel pointer out of valid range: 0x{:X}", hudDataModelAddr);
        return nullptr;
    }

    // HUDQuickContainerDataModel is at HUDDataModel + 0x38
    void* dataModel = reinterpret_cast<void*>(hudDataModelAddr + 0x38);

    // Verify via RTTI with exact type matching
    const char* typeName = GetRTTITypeName(dataModel);
    if (isValidRTTI(typeName)) {
        m_hudQuickContainerDataModelPtr = dataModel;
        REX::INFO("HUDMenuHook: Found HUDQuickContainerDataModel via singleton at 0x{:X}",
            reinterpret_cast<std::uintptr_t>(dataModel));
        return dataModel;
    }

    REX::DEBUG("HUDMenuHook: RTTI verification failed for DataModel at HUDDataModel+0x38");
    return nullptr;
}

void HUDMenuHook::CallNativeQuickContainerUp(void* a_dataModel)
{
    if (!a_dataModel) return;

    // QuickContainerUp is at RVA 0xa0aa70 (from Ghidra: FUN_140a0aa70)
    // Function signature: void __fastcall QuickContainerUp(HUDQuickContainerDataModel* this)
    using QuickContainerUpFunc = void(__fastcall*)(void*);
    static REL::Relocation<QuickContainerUpFunc> quickContainerUp{ REL::Offset(0xa0aa70) };

    quickContainerUp(a_dataModel);
}

void HUDMenuHook::CallNativeQuickContainerDown(void* a_dataModel)
{
    if (!a_dataModel) return;

    // QuickContainerDown is at RVA 0xa0aab0 (from Ghidra: FUN_140a0aab0)
    // Function signature: void __fastcall QuickContainerDown(HUDQuickContainerDataModel* this)
    using QuickContainerDownFunc = void(__fastcall*)(void*);
    static REL::Relocation<QuickContainerDownFunc> quickContainerDown{ REL::Offset(0xa0aab0) };

    quickContainerDown(a_dataModel);
}
