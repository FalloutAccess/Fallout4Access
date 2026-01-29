#pragma once

#include "PipBoyPageBase.h"

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
    // Tab definitions
    enum class Tab {
        Quests = 0,
        Workshops = 1,
        Stats = 2
    };

    const char* GetTabName(std::int32_t a_tabIndex);
};
