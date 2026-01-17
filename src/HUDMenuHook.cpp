#include "HUDMenuHook.h"
#include <SRAL.h>
#include <RE/H/HUDMenu.h>

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
        m_installed = true;
        REX::INFO("HUDMenuHook: AdvanceMovie hook installed successfully");
    } else {
        REX::ERROR("HUDMenuHook: Failed to install AdvanceMovie hook");
    }
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

    // Periodic diagnostic logging (every ~5 seconds at 60fps)
    static int frameCount = 0;
    if (++frameCount % 300 == 0) {
        REX::DEBUG("HUDMenuHook: CheckNotifications running (frame {})", frameCount);
    }

    CheckMessages(a_menu);
    CheckQuestUpdates(a_menu);
    CheckObjectiveUpdates(a_menu);
    CheckXPUpdates(a_menu);
    CheckTutorialText(a_menu);
    CheckLevelUp(a_menu);
    CheckSubtitles(a_menu);

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
        static bool loggedOnce = false;
        if (!loggedOnce) {
            REX::DEBUG("HUDMenuHook: Failed to get Messages_mc at path: {}", MESSAGES_MC_PATH);
            loggedOnce = true;
        }
        return;
    }
    if (!messagesClip.IsObject()) {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            REX::DEBUG("HUDMenuHook: Messages_mc is not an object, type={}", static_cast<int>(messagesClip.GetType()));
            loggedOnce = true;
        }
        return;
    }

    // Get ShownMessageArray - this is a Vector.<HUDMessageItem>
    Scaleform::GFx::Value shownArray;
    if (!messagesClip.GetMember("ShownMessageArray", &shownArray)) {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            REX::DEBUG("HUDMenuHook: Failed to get ShownMessageArray member");
            loggedOnce = true;
        }
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
    } else {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            REX::DEBUG("HUDMenuHook: ShownMessageArray is not array/object, type={}", static_cast<int>(shownArray.GetType()));
            loggedOnce = true;
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

    // Periodic logging of message counts (every ~5 seconds)
    static int msgCheckCount = 0;
    if (++msgCheckCount % 300 == 0) {
        REX::DEBUG("HUDMenuHook: Messages shown={}, queued={}, lastCount={}", count, queueCount, m_lastMessageCount);
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
    if (a_text.empty()) return;

    if (SRAL_GetCurrentEngine() != 0) {
        SRAL_Speak(a_text.c_str(), false);  // Never interrupt per CLAUDE.md
    }
}
