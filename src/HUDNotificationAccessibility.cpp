#include "HUDNotificationAccessibility.h"
#include <SRAL.h>
#include <thread>

HUDNotificationAccessibility* HUDNotificationAccessibility::GetSingleton()
{
    static HUDNotificationAccessibility singleton;
    return &singleton;
}

std::uint64_t HUDNotificationAccessibility::GetCurrentTimeMs() const
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

void HUDNotificationAccessibility::OnHUDMenuOpened()
{
    REX::INFO("HUDNotificationAccessibility: HUD opened");
    m_lastMessageCount = 0;
    m_lastObjectiveCount = 0;
    m_lastQuestUpdate.clear();
    m_lastXPValue.clear();
    m_isOpen.store(true);
    StartPollingThread();
}

void HUDNotificationAccessibility::OnHUDMenuClosed()
{
    REX::INFO("HUDNotificationAccessibility: HUD closed");
    m_isOpen.store(false);
    m_announcementHistory.clear();
}

void HUDNotificationAccessibility::StartPollingThread()
{
    // Use background thread with real sleep to avoid task queue flooding
    // This follows the pattern from TerminalMenuAccessibility::OnAcceptPressed
    std::thread([this]() {
        while (m_isOpen.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));

            if (!m_isOpen.load()) break;

            // Schedule the actual check on the main thread (Scaleform requires main thread)
            if (auto* taskInterface = F4SE::GetTaskInterface()) {
                taskInterface->AddTask([this]() {
                    if (m_isOpen.load()) {
                        PollNotifications();
                    }
                });
            }
        }
    }).detach();
}

void HUDNotificationAccessibility::PollNotifications()
{
    auto* ui = RE::UI::GetSingleton();
    if (!ui) {
        return;
    }

    // Don't poll while in menu mode (pause menu, etc.)
    if (ui->menuMode) {
        return;
    }

    auto menu = ui->GetMenu("HUDMenu"sv);
    if (!menu || !menu->uiMovie) {
        return;
    }

    // Check all notification types
    CheckMessages(menu.get());
    CheckQuestUpdates(menu.get());
    CheckObjectiveUpdates(menu.get());
    CheckXPUpdates(menu.get());

    // Clean old entries from deduplication cache
    CleanExpiredAnnouncements();
}

void HUDNotificationAccessibility::CheckMessages(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return;
    }

    Scaleform::GFx::Value messageArray;
    if (!a_menu->uiMovie->GetVariable(&messageArray, MESSAGES_ARRAY_PATH) || !messageArray.IsArray()) {
        return;
    }

    std::uint32_t count = messageArray.GetArraySize();

    // Handle array reset (count < last count means array was cleared)
    if (count < m_lastMessageCount) {
        m_lastMessageCount = 0;
    }

    // If we have new messages since last check
    if (count > m_lastMessageCount) {
        // Announce new messages (from last seen to current)
        for (std::uint32_t i = m_lastMessageCount; i < count; ++i) {
            Scaleform::GFx::Value item;
            if (!messageArray.GetElement(i, &item) || !item.IsObject()) {
                continue;
            }

            Scaleform::GFx::Value textVal;
            if (item.GetMember("messageText", &textVal) && textVal.IsString()) {
                std::string text = textVal.GetString();
                if (!text.empty() && ShouldAnnounce(text)) {
                    RecordAnnouncement(text);
                    SpeakText(text);
                }
            }
        }
    }

    m_lastMessageCount = count;
}

void HUDNotificationAccessibility::CheckQuestUpdates(RE::IMenu* a_menu)
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

    if (name.empty() && type.empty()) {
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
            RecordAnnouncement(announcement);
            SpeakText(announcement);
        }
    }
}

void HUDNotificationAccessibility::CheckObjectiveUpdates(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return;
    }

    Scaleform::GFx::Value objectiveArray;
    if (!a_menu->uiMovie->GetVariable(&objectiveArray, OBJECTIVES_ARRAY_PATH) || !objectiveArray.IsArray()) {
        return;
    }

    std::uint32_t count = objectiveArray.GetArraySize();

    // Handle array reset (count < last count means array was cleared)
    if (count < m_lastObjectiveCount) {
        m_lastObjectiveCount = 0;
    }

    // If we have new objectives since last check
    if (count > m_lastObjectiveCount) {
        // Announce new objectives (from last seen to current)
        for (std::uint32_t i = m_lastObjectiveCount; i < count; ++i) {
            Scaleform::GFx::Value item;
            if (!objectiveArray.GetElement(i, &item) || !item.IsObject()) {
                continue;
            }

            Scaleform::GFx::Value msgVal;
            if (item.GetMember("ObjectiveMessage", &msgVal) && msgVal.IsString()) {
                std::string text = msgVal.GetString();
                if (!text.empty()) {
                    // Check if this objective is completed
                    Scaleform::GFx::Value completedVal;
                    bool isCompleted = false;
                    if (item.GetMember("isCompleted", &completedVal) && completedVal.IsBoolean()) {
                        isCompleted = completedVal.GetBoolean();
                    }

                    std::string announcement = isCompleted ? "Completed: " + text : text;

                    // Dedupe on the full announcement (includes "Completed:" prefix)
                    if (ShouldAnnounce(announcement)) {
                        RecordAnnouncement(announcement);
                        SpeakText(announcement);
                    }
                }
            }
        }
    }

    m_lastObjectiveCount = count;
}

void HUDNotificationAccessibility::CheckXPUpdates(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return;
    }

    Scaleform::GFx::Value xpValue;
    if (!a_menu->uiMovie->GetVariable(&xpValue, XP_NUMBER_PATH) || !xpValue.IsString()) {
        return;
    }

    std::string xpText = xpValue.GetString();

    // XP text is empty or zero when not showing, only announce when there's a real value
    // Check both empty and numeric zero (handles "0", "00", etc.)
    if (xpText.empty()) {
        m_lastXPValue.clear();
        return;
    }

    // Try to parse as number and skip if zero
    try {
        int xpNum = std::stoi(xpText);
        if (xpNum <= 0) {
            m_lastXPValue.clear();
            return;
        }
    } catch (...) {
        // If parsing fails, skip this value
        m_lastXPValue.clear();
        return;
    }

    // Check if this is a new/different XP value
    if (xpText != m_lastXPValue) {
        m_lastXPValue = xpText;

        // Build announcement (e.g., "+25 XP")
        std::string announcement = "+" + xpText + " XP";

        if (ShouldAnnounce(announcement)) {
            RecordAnnouncement(announcement);
            SpeakText(announcement);
        }
    }
}

bool HUDNotificationAccessibility::ShouldAnnounce(const std::string& a_text)
{
    if (a_text.empty()) {
        return false;
    }

    auto it = m_announcementHistory.find(a_text);
    if (it == m_announcementHistory.end()) {
        return true;  // Never announced
    }

    auto now = GetCurrentTimeMs();
    return (now - it->second) >= kDeduplicationWindowMs;
}

void HUDNotificationAccessibility::RecordAnnouncement(const std::string& a_text)
{
    m_announcementHistory[a_text] = GetCurrentTimeMs();
}

void HUDNotificationAccessibility::CleanExpiredAnnouncements()
{
    auto now = GetCurrentTimeMs();
    for (auto it = m_announcementHistory.begin(); it != m_announcementHistory.end();) {
        if ((now - it->second) > kDeduplicationWindowMs * 2) {  // Clean after 4 seconds
            it = m_announcementHistory.erase(it);
        } else {
            ++it;
        }
    }
}

void HUDNotificationAccessibility::SpeakText(const std::string& a_text)
{
    if (SRAL_GetCurrentEngine() != 0) {
        SRAL_Speak(a_text.c_str(), false);
        REX::INFO("HUD Notification: {}", a_text);
    }
}
