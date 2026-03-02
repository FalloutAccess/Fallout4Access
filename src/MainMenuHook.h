#pragma once

#include "pch.h"
#include <RE/M/MainMenu.h>
#include <RE/P/PauseMenu.h>

// Hooks MainMenu and PauseMenu AdvanceMovie to check for state changes every frame
// This replaces unreliable keystroke polling with efficient per-frame checks
// Note: Both MainMenu and PauseMenu use the same SWF (MainMenu.swf)
class MainMenuHook
{
public:
    static MainMenuHook* GetSingleton();

    // Install/uninstall the vtable hook
    void Install();
    void Uninstall();

    // Check if hook is currently active
    bool IsInstalled() const { return m_installed; }

    // Set which menu is currently open (called by MenuAccessibility)
    void SetMainMenuOpen(bool a_open);
    void SetPauseMenuOpen(bool a_open);

    // Reset state tracking (call when menu opens)
    void ResetState();

    // Check main list selection (called from keystroke handler)
    void CheckMainListSelection();

    // Check for confirm dialog (called from AdvanceMovie hook)
    void CheckConfirmDialog();

private:
    MainMenuHook() = default;
    ~MainMenuHook() = default;
    MainMenuHook(const MainMenuHook&) = delete;
    MainMenuHook(MainMenuHook&&) = delete;
    MainMenuHook& operator=(const MainMenuHook&) = delete;
    MainMenuHook& operator=(MainMenuHook&&) = delete;

    // The hooked AdvanceMovie function
    static void HookedAdvanceMovie(RE::MainMenu* a_this, float a_timeDelta, std::uint64_t a_time);

    // Which panel is currently active
    enum class ActivePanel
    {
        None,
        SplashScreen,
        MainList,
        Settings,
        SaveLoad,
        Confirm
    };

    // State checking
    void CheckStateChanges(RE::MainMenu* a_menu);
    ActivePanel DetectActivePanel(RE::MainMenu* a_menu);

    // Panel-specific handlers
    void OnPanelChanged(RE::MainMenu* a_menu, ActivePanel a_newPanel);
    void OnSplashScreen(RE::MainMenu* a_menu);
    void OnMainListSelection(RE::MainMenu* a_menu);
    void CheckMainListSelectionFromClips(RE::MainMenu* a_menu);
    void OnConfirmDialog(RE::MainMenu* a_menu);

    // Panel visibility checks
    bool IsPanelVisible(RE::MainMenu* a_menu, const char* a_panelPath);
    bool IsMainPanelVisible(RE::MainMenu* a_menu);

    // Scaleform helpers
    std::int32_t GetSelectedIndex(RE::MainMenu* a_menu);
    std::int32_t GetSelectedIndexFromClips(RE::MainMenu* a_menu);
    std::string GetSelectedItemText(RE::MainMenu* a_menu, std::int32_t a_index);
    std::string GetConfirmText(RE::MainMenu* a_menu);
    std::string GetSplashText(RE::MainMenu* a_menu);

    // Utility
    void SpeakText(const std::string& a_text);

    // State
    bool m_installed = false;
    bool m_mainMenuOpen = false;
    bool m_pauseMenuOpen = false;
    ActivePanel m_lastPanel = ActivePanel::None;
    std::int32_t m_lastSelection = -1;
    std::string m_lastConfirmText;
    bool m_splashAnnounced = false;
    bool m_menuNameAnnounced = false;

    // Scaleform paths
    static constexpr const char* MAIN_PANEL_PATH = "root.Menu_mc.MainPanel_mc";
    static constexpr const char* MAIN_LIST_PATH = "root.Menu_mc.MainPanel_mc.List_mc";
    static constexpr const char* SETTINGS_PANEL_PATH = "root.Menu_mc.SettingsPanel_mc";
    static constexpr const char* SAVELOAD_PANEL_PATH = "root.Menu_mc.SaveLoadHolder_mc";
    static constexpr const char* CONFIRM_PANEL_PATH = "root.Menu_mc.ConfirmPanel_mc";
    static constexpr const char* CONFIRM_TEXT_PATH = "root.Menu_mc.ConfirmPanel_mc.Text_mc.TextPulse_mc.textField.text";
    static constexpr const char* SPLASH_TEXT_PATH = "root.Menu_mc.SplashScreenHolder_mc.SplashScreenText_tf.text";

    // The vtable hooks
    static inline REL::HookVFT<void(RE::MainMenu*, float, std::uint64_t)>* s_advanceMovieHook = nullptr;
    static inline REL::HookVFT<void(RE::PauseMenu*, float, std::uint64_t)>* s_pauseMenuAdvanceMovieHook = nullptr;
};
