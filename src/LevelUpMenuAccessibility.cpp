#include "LevelUpMenuAccessibility.h"
#include "LevelUpMenuHook.h"
#include "SpeechManager.h"
#include "LocalizationHelper.h"
#include <Scaleform/G/GFx_Value.h>

using Scaleform::GFx::Value;

LevelUpMenuAccessibility* LevelUpMenuAccessibility::GetSingleton()
{
    static LevelUpMenuAccessibility singleton;
    return &singleton;
}

void LevelUpMenuAccessibility::OnLevelUpMenuOpened()
{
    m_isOpen = true;
    m_menuAnnounced = false;
    m_lastRow = 0xFFFFFFFF;
    m_lastCol = 0xFFFFFFFF;

    REX::INFO("LevelUpMenuAccessibility: Menu opened");

    // Install and reset the hook
    LevelUpMenuHook::GetSingleton()->Install();
    LevelUpMenuHook::GetSingleton()->ResetState();
    LevelUpMenuHook::GetSingleton()->SetMenuOpen(true);

    // Schedule menu announcement for after Scaleform populates
    if (auto* taskInterface = F4SE::GetTaskInterface()) {
        // Wait a few frames for data to be ready
        taskInterface->AddTask([this]() {
            if (auto* taskInterface2 = F4SE::GetTaskInterface()) {
                taskInterface2->AddTask([this]() {
                    if (auto* taskInterface3 = F4SE::GetTaskInterface()) {
                        taskInterface3->AddTask([this]() {
                            if (!m_isOpen) return;

                            auto* ui = RE::UI::GetSingleton();
                            if (!ui) return;

                            auto menu = ui->GetMenu("LevelUpMenu"sv);
                            if (!menu || !menu->uiMovie) return;

                            std::string announcement = BuildMenuOpenAnnouncement(menu.get());
                            if (!announcement.empty()) {
                                SpeakText(announcement);
                                m_menuAnnounced = true;
                            }
                        });
                    }
                });
            }
        });
    }
}

void LevelUpMenuAccessibility::OnLevelUpMenuClosed()
{
    m_isOpen = false;
    LevelUpMenuHook::GetSingleton()->SetMenuOpen(false);
    Reset();
    REX::INFO("LevelUpMenuAccessibility: Menu closed");
}

void LevelUpMenuAccessibility::Reset()
{
    m_menuAnnounced = false;
    m_lastRow = 0xFFFFFFFF;
    m_lastCol = 0xFFFFFFFF;
}

void LevelUpMenuAccessibility::AnnounceSelection(std::uint32_t a_row, std::uint32_t a_col)
{
    if (!m_isOpen) return;

    // Note: Duplicate check is now handled by LevelUpMenuHook which tracks row/col/rankOffset
    m_lastRow = a_row;
    m_lastCol = a_col;

    auto* ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("LevelUpMenu"sv);
    if (!menu || !menu->uiMovie) return;

    std::string announcement = BuildSelectionAnnouncement(menu.get(), a_row, a_col);
    if (!announcement.empty()) {
        SpeakText(announcement);
    }
}

void LevelUpMenuAccessibility::AnnounceConfirmation(bool a_confirming)
{
    if (!m_isOpen) return;

    if (a_confirming) {
        SpeakText(LocalizationHelper::Translate("$F4A_confirming_perk"));
    }
}

std::string LevelUpMenuAccessibility::BuildMenuOpenAnnouncement(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    std::string announcement = LocalizationHelper::Translate("$F4A_levelup");

    // Get player level
    Value levelValue;
    if (a_menu->uiMovie->GetVariable(&levelValue, PLAYER_LEVEL_PATH)) {
        int level = 0;
        if (levelValue.IsUInt()) {
            level = static_cast<int>(levelValue.GetUInt());
        } else if (levelValue.IsInt()) {
            level = levelValue.GetInt();
        } else if (levelValue.IsNumber()) {
            level = static_cast<int>(levelValue.GetNumber());
        }

        if (level > 0) {
            announcement += ", " + LocalizationHelper::Translate("$F4A_level") + " " + std::to_string(level);
        }
    }

    // Get perk points available
    Value perkPointsValue;
    if (a_menu->uiMovie->GetVariable(&perkPointsValue, PERK_POINTS_PATH)) {
        int perkPoints = 0;
        if (perkPointsValue.IsUInt()) {
            perkPoints = static_cast<int>(perkPointsValue.GetUInt());
        } else if (perkPointsValue.IsInt()) {
            perkPoints = perkPointsValue.GetInt();
        } else if (perkPointsValue.IsNumber()) {
            perkPoints = static_cast<int>(perkPointsValue.GetNumber());
        }

        if (perkPoints > 0) {
            std::string perkPointStr = (perkPoints == 1)
                ? LocalizationHelper::Translate("$F4A_perk_point")
                : LocalizationHelper::Translate("$F4A_perk_points");
            announcement += ", " + std::to_string(perkPoints) + " " + perkPointStr;
        }
    }

    return announcement;
}

std::string LevelUpMenuAccessibility::BuildSelectionAnnouncement(RE::IMenu* a_menu, std::uint32_t a_row, std::uint32_t a_col)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    std::string announcement;

    // Get SelectedPerkEntry object which contains perk data
    Value selectedPerk;
    if (!a_menu->uiMovie->GetVariable(&selectedPerk, SELECTED_PERK_PATH) || !selectedPerk.IsObject()) {
        // If no selected perk, just announce position
        announcement = GetColumnName(a_col);
        announcement += " " + LocalizationHelper::Translate("$F4A_column");
        return announcement;
    }

    // Get perk name
    Value textValue;
    if (selectedPerk.GetMember("text", &textValue) && textValue.IsString()) {
        const char* perkName = textValue.GetString();
        if (perkName && perkName[0] != '\0') {
            announcement = perkName;
        }
    }

    // Add column name (SPECIAL stat)
    announcement += ", " + std::string(GetColumnName(a_col)) + " " + LocalizationHelper::Translate("$F4A_column");

    // Get rank info by reading star states
    RankInfo rankInfo = GetRankInfoFromStars(a_menu);

    if (rankInfo.maxRank > 1) {
        // Multi-rank perk - show rank info
        // Note: We can't reliably track viewed rank when using Alt/Ctrl
        // So we show owned count instead of trying to say if current rank is owned
        announcement += ", " + std::to_string(rankInfo.ownedRanks) + " " +
            LocalizationHelper::Translate("$F4A_of") + " " +
            std::to_string(rankInfo.maxRank) + " " +
            LocalizationHelper::Translate("$F4A_ranks_owned");
    } else if (rankInfo.maxRank == 1) {
        // Single-rank perk - check if owned
        Value ownedValue;
        if (selectedPerk.GetMember("owned", &ownedValue) && ownedValue.IsBoolean()) {
            if (ownedValue.GetBoolean()) {
                announcement += ", " + LocalizationHelper::Translate("$F4A_owned");
            }
        }
    } else {
        // Fallback if no stars found - use perk entry data
        Value ownedValue;
        if (selectedPerk.GetMember("owned", &ownedValue) && ownedValue.IsBoolean()) {
            if (ownedValue.GetBoolean()) {
                announcement += ", " + LocalizationHelper::Translate("$F4A_owned");
            }
        }
    }

    // Check if available (can acquire next rank now)
    Value availableValue;
    if (selectedPerk.GetMember("available", &availableValue) && availableValue.IsBoolean()) {
        if (!availableValue.GetBoolean()) {
            announcement += ", " + LocalizationHelper::Translate("$F4A_unavailable");
        }
    }

    // Get requirements text (shows "Requires: X" for the current rank)
    Value reqTextValue;
    if (a_menu->uiMovie->GetVariable(&reqTextValue, REQUIREMENTS_TEXT_PATH) && reqTextValue.IsString()) {
        const char* req = reqTextValue.GetString();
        if (req && req[0] != '\0' && req[0] != ' ') {
            // Requirements text exists and is not empty/whitespace
            announcement += ". " + std::string(req);
        }
    }

    // Get description from the text field (updated when viewing different ranks via Alt/Ctrl)
    // Fall back to selectedPerk.description if text field is empty
    std::string descStr;

    Value descTextValue;
    if (a_menu->uiMovie->GetVariable(&descTextValue, DESCRIPTION_TEXT_PATH) && descTextValue.IsString()) {
        const char* desc = descTextValue.GetString();
        if (desc && desc[0] != '\0') {
            descStr = desc;
        }
    }

    // Fallback to perk entry description if text field is empty
    if (descStr.empty()) {
        Value descValue;
        if (selectedPerk.GetMember("description", &descValue) && descValue.IsString()) {
            const char* desc = descValue.GetString();
            if (desc && desc[0] != '\0') {
                descStr = desc;
            }
        }
    }

    if (!descStr.empty()) {
        // Replace newlines with periods for better speech
        for (size_t i = 0; i < descStr.size(); ++i) {
            if (descStr[i] == '\n') {
                descStr[i] = '.';
                // Add space after period if not at end
                if (i + 1 < descStr.size() && descStr[i + 1] != ' ') {
                    descStr.insert(i + 1, " ");
                }
            }
        }
        announcement += ". " + descStr;
    }

    return announcement;
}

const char* LevelUpMenuAccessibility::GetColumnName(std::uint32_t a_col)
{
    // Use game's ActorValue singleton to get localized SPECIAL names
    RE::ActorValue* av = RE::ActorValue::GetSingleton();
    if (av) {
        RE::ActorValueInfo* info = nullptr;
        switch (a_col) {
            case 0: info = av->strength; break;
            case 1: info = av->perception; break;
            case 2: info = av->endurance; break;
            case 3: info = av->charisma; break;
            case 4: info = av->intelligence; break;
            case 5: info = av->agility; break;
            case 6: info = av->luck; break;
        }
        if (info) {
            const char* name = info->GetFullName();
            if (name && name[0] != '\0') {
                return name;
            }
        }
    }
    // Fallback to hardcoded names if lookup fails
    if (a_col < 7) {
        return SPECIAL_NAMES[a_col];
    }
    return "Unknown";
}

LevelUpMenuAccessibility::RankInfo LevelUpMenuAccessibility::GetRankInfoFromStars(RE::IMenu* a_menu)
{
    RankInfo info;
    if (!a_menu || !a_menu->uiMovie) return info;

    // Get StarHolder_mc
    Value starHolder;
    if (!a_menu->uiMovie->GetVariable(&starHolder, STAR_HOLDER_PATH) || !starHolder.IsObject()) {
        return info;
    }

    // Get number of stars (children)
    Value numChildrenValue;
    if (!starHolder.GetMember("numChildren", &numChildrenValue)) {
        return info;
    }

    int numChildren = 0;
    if (numChildrenValue.IsUInt()) {
        numChildren = static_cast<int>(numChildrenValue.GetUInt());
    } else if (numChildrenValue.IsInt()) {
        numChildren = numChildrenValue.GetInt();
    } else if (numChildrenValue.IsNumber()) {
        numChildren = static_cast<int>(numChildrenValue.GetNumber());
    }

    if (numChildren <= 0) {
        return info;
    }

    info.maxRank = numChildren;

    // Iterate through each star to find states
    // Star labels: "Full" = owned, "Available" = next to purchase, "Empty" = future
    // Note: "Available" marks the NEXT rank to purchase, NOT the currently viewed rank
    // When user presses Alt/Ctrl, star labels don't change - only description changes
    for (int i = 0; i < numChildren; ++i) {
        // Call getChildAt(i) to get the star MovieClip
        Value star;
        Value args[1];
        args[0] = static_cast<double>(i);

        if (!starHolder.Invoke("getChildAt", &star, args, 1) || !star.IsObject()) {
            continue;
        }

        // Read currentFrameLabel to get the star state
        Value frameLabelValue;
        if (!star.GetMember("currentFrameLabel", &frameLabelValue) || !frameLabelValue.IsString()) {
            continue;
        }

        const char* frameLabel = frameLabelValue.GetString();
        if (!frameLabel) continue;

        std::string label = frameLabel;

        if (label == "Full") {
            info.ownedRanks++;
        } else if (label == "Available") {
            // "Available" marks the next rank to purchase (ownedRanks + 1)
            // This is NOT necessarily the viewed rank when using Alt/Ctrl
            info.viewedRank = i + 1;
        }
    }

    // We can reliably determine:
    // - maxRank: total number of stars
    // - ownedRanks: count of "Full" stars
    // We CANNOT reliably determine the viewed rank when using Alt/Ctrl
    // because star labels don't change - only description changes

    // If "Available" star found, viewedRank is set to that (next purchasable rank)
    // If no "Available" (all owned or none owned), estimate from owned count
    if (info.viewedRank == 0) {
        if (info.ownedRanks >= info.maxRank) {
            // All ranks owned - default to max rank
            info.viewedRank = info.maxRank;
        } else if (info.ownedRanks > 0) {
            // Some owned - next rank would be ownedRanks + 1
            info.viewedRank = info.ownedRanks + 1;
        } else {
            // None owned - first rank
            info.viewedRank = 1;
        }
    }

    // isViewedOwned: we can only know this for the "base" view
    // When Alt/Ctrl is used, we can't track which rank is being viewed
    // So we DON'T set isViewedOwned here - let the caller handle it
    // based on comparing viewedRank to ownedRanks if needed
    info.isViewedOwned = false;  // Default to false, don't assume ownership

    return info;
}

void LevelUpMenuAccessibility::SpeakText(const std::string& a_text)
{
    SpeechManager::GetSingleton()->Speak(a_text);
}
