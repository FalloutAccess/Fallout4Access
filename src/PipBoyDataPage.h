#pragma once

#include "PipBoyPageBase.h"
#include <vector>
#include <string>

// Handles the DATA page of the Pip-Boy
// Tabs: Quests(0), Workshops(1), Stats(2)
class PipBoyDataPage : public PipBoyPageBase
{
public:
    PipBoyDataPage() = default;
    ~PipBoyDataPage() override = default;

    // PipBoyPageBase interface
    void OnPageActivated() override;
    void OnPageDeactivated() override;
    void CheckSelection() override;
    void CheckTabChange() override;
    void Reset() override;
    const char* GetPageName() const override { return "Data"; }

private:
    // Quest data
    struct QuestInfo {
        std::string text;              // Quest name
        std::string description;       // Quest description
        bool active = false;           // Currently tracked
        bool completed = false;        // Quest completed
        bool isDivider = false;        // Is a divider line (not a real quest)
        std::uint32_t formID = 0;      // Quest form ID (0 = misc objective container)
    };

    // Workshop data
    struct WorkshopInfo {
        std::string text;              // Settlement name
        float happiness = 0.0f;
        std::int32_t population = 0;
    };

    // Tab definitions
    enum class Tab {
        Quests = 0,
        Workshops = 1,
        Stats = 2
    };

    // Data retrieval
    QuestInfo GetQuestInfo(RE::IMenu* a_menu, std::int32_t a_index);
    WorkshopInfo GetWorkshopInfo(RE::IMenu* a_menu, std::int32_t a_index);
    std::string GetStatInfo(RE::IMenu* a_menu, std::int32_t a_index);

    // Announcement building
    std::string BuildQuestAnnouncement(const QuestInfo& a_quest);
    std::string BuildWorkshopAnnouncement(const WorkshopInfo& a_workshop);
    const char* GetTabName(std::int32_t a_tabIndex);
};
