#include "LevelUpMenuHook.h"
#include "LevelUpMenuAccessibility.h"
#include <Scaleform/G/GFx_Value.h>

using Scaleform::GFx::Value;

LevelUpMenuHook* LevelUpMenuHook::GetSingleton()
{
    static LevelUpMenuHook singleton;
    return &singleton;
}

void LevelUpMenuHook::Install()
{
    if (m_installed) {
        return;
    }

    // Hook LevelUpMenu::AdvanceMovie (vtable index 4)
    // VTABLE::LevelUpMenu[0] is the primary vtable (IMenu/GameMenuBase)
    static REL::THookVFT<void(RE::IMenu*, float, std::uint64_t)> hook(
        VTABLE::LevelUpMenu[0],
        4,  // AdvanceMovie is at index 4
        HookedAdvanceMovie
    );

    if (hook.Enable()) {
        s_advanceMovieHook = &hook;
        m_installed = true;
        REX::INFO("LevelUpMenuHook: AdvanceMovie hook installed successfully");
    } else {
        REX::ERROR("LevelUpMenuHook: Failed to install AdvanceMovie hook");
    }
}

void LevelUpMenuHook::ResetState()
{
    m_hotpatchApplied = false;
    m_gridViewErrorLogged = false;
    m_lastRow = 0xFFFFFFFF;
    m_lastCol = 0xFFFFFFFF;
    m_lastDescription.clear();
    m_frameDelay = FRAME_DELAY_AFTER_OPEN;
}

void LevelUpMenuHook::HookedAdvanceMovie(RE::IMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
    // Call original function first to let the game update state
    if (s_advanceMovieHook) {
        (*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
    }

    // Then check for state changes
    auto* hook = GetSingleton();
    if (hook->m_menuOpen) {
        hook->CheckStateChanges(a_this);
    }
}

void LevelUpMenuHook::CheckStateChanges(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return;

    // Wait for frame delay after menu open
    if (m_frameDelay > 0) {
        m_frameDelay--;
        return;
    }

    // Apply platform hotpatch once to enable keyboard navigation
    if (!m_hotpatchApplied) {
        ApplyPlatformHotpatch(a_menu);
    }

    // Only check selection if hotpatch was applied (GridView was found)
    if (m_hotpatchApplied) {
        CheckSelectionChange(a_menu);
    }
}

void LevelUpMenuHook::ApplyPlatformHotpatch(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return;

    // The LevelUpMenu's PerkGrid.as explicitly blocks keyboard input when
    // iPlatform == PlatformChangeEvent.PLATFORM_PC_KB_MOUSE (which is 0).
    // By setting platform to 1 (PLATFORM_PC_GAMEPAD), we trick the menu
    // into accepting keyboard input via the arrow key handling code path.

    // Get GridView_mc
    Value gridView;
    if (!a_menu->uiMovie->GetVariable(&gridView, GRID_VIEW_PATH)) {
        if (!m_gridViewErrorLogged) {
            REX::ERROR("LevelUpMenuHook: GridView_mc not found at {}", GRID_VIEW_PATH);
            m_gridViewErrorLogged = true;
        }
        return;
    }

    if (!gridView.IsObject()) {
        if (!m_gridViewErrorLogged) {
            REX::ERROR("LevelUpMenuHook: GridView_mc is not an object");
            m_gridViewErrorLogged = true;
        }
        return;
    }

    // Call SetPlatform(1, false) to switch to gamepad mode
    // SetPlatform is from BSUIComponent: SetPlatform(param1:Number, param2:Boolean)
    Value result;
    Value args[2];
    args[0] = 1.0;    // PLATFORM_PC_GAMEPAD (Number)
    args[1] = false;  // bPS3Switch (Boolean)

    if (gridView.Invoke("SetPlatform", &result, args, 2)) {
        m_hotpatchApplied = true;
        REX::INFO("LevelUpMenuHook: Platform hotpatch applied - keyboard navigation enabled");
    } else {
        // Fallback: try setting platform property directly via setter
        Value platformValue(static_cast<std::uint32_t>(1));
        if (gridView.SetMember("platform", platformValue)) {
            m_hotpatchApplied = true;
            REX::INFO("LevelUpMenuHook: Platform property set directly - keyboard navigation enabled");
        } else {
            REX::ERROR("LevelUpMenuHook: Failed to apply platform hotpatch");
        }
    }
}

void LevelUpMenuHook::CheckSelectionChange(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return;

    // Check if selectedPerkClip exists (may be null until user navigates)
    Value selectedClip;
    if (!a_menu->uiMovie->GetVariable(&selectedClip, SELECTED_PERK_CLIP_PATH)) {
        return;  // No clip selected yet
    }

    if (!selectedClip.IsObject()) {
        return;  // Clip is null or undefined
    }

    // Get row from selectedPerkClip
    Value rowValue;
    std::uint32_t currentRow = 0xFFFFFFFF;
    if (selectedClip.GetMember("row", &rowValue)) {
        if (rowValue.IsUInt()) {
            currentRow = rowValue.GetUInt();
        } else if (rowValue.IsInt()) {
            int intVal = rowValue.GetInt();
            if (intVal >= 0) {
                currentRow = static_cast<std::uint32_t>(intVal);
            }
        } else if (rowValue.IsNumber()) {
            double numVal = rowValue.GetNumber();
            if (numVal >= 0) {
                currentRow = static_cast<std::uint32_t>(numVal);
            }
        }
    }

    // Get col from selectedPerkClip
    Value colValue;
    std::uint32_t currentCol = 0xFFFFFFFF;
    if (selectedClip.GetMember("col", &colValue)) {
        if (colValue.IsUInt()) {
            currentCol = colValue.GetUInt();
        } else if (colValue.IsInt()) {
            int intVal = colValue.GetInt();
            if (intVal >= 0) {
                currentCol = static_cast<std::uint32_t>(intVal);
            }
        } else if (colValue.IsNumber()) {
            double numVal = colValue.GetNumber();
            if (numVal >= 0) {
                currentCol = static_cast<std::uint32_t>(numVal);
            }
        }
    }

    // Get description text (changes when Alt/Ctrl pressed to switch ranks)
    Value descValue;
    std::string currentDescription;
    if (a_menu->uiMovie->GetVariable(&descValue, DESCRIPTION_TEXT_PATH)) {
        if (descValue.IsString()) {
            const char* desc = descValue.GetString();
            if (desc) {
                currentDescription = desc;
            }
        }
    }

    // Check if selection or description changed
    if (currentRow != 0xFFFFFFFF && currentCol != 0xFFFFFFFF) {
        bool selectionChanged = (currentRow != m_lastRow || currentCol != m_lastCol);
        bool descriptionChanged = (!currentDescription.empty() && currentDescription != m_lastDescription);

        if (selectionChanged || descriptionChanged) {
            m_lastRow = currentRow;
            m_lastCol = currentCol;
            m_lastDescription = currentDescription;

            // Notify accessibility class of selection change
            LevelUpMenuAccessibility::GetSingleton()->AnnounceSelection(currentRow, currentCol);
        }
    }
}
