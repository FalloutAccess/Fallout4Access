#pragma once

#include "pch.h"

class MenuAccessibility :
    public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
    public RE::BSInputEventUser
{
public:
    static MenuAccessibility* GetSingleton();

    void Register();

    // Menu events
    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent& a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source) override;

    // Input events (BSInputEventUser)
    bool ShouldHandleEvent(const RE::InputEvent* a_event) override;
    void OnButtonEvent(const RE::ButtonEvent* a_event) override;

private:
    MenuAccessibility() = default;
    ~MenuAccessibility() override = default;
    MenuAccessibility(const MenuAccessibility&) = delete;
    MenuAccessibility(MenuAccessibility&&) = delete;
    MenuAccessibility& operator=(const MenuAccessibility&) = delete;
    MenuAccessibility& operator=(MenuAccessibility&&) = delete;

    void OnMenuOpened(const RE::BSFixedString& a_menuName);
    void OnMenuClosed(const RE::BSFixedString& a_menuName);
    void CheckAndSpeakSelection();
    void CheckForConfirmationDialog();
    void DumpScaleformChildren(Scaleform::GFx::Value& a_obj, const std::string& a_path, int a_depth = 0);
    std::string GetSelectedItemText(RE::IMenu* a_menu, const std::string& a_listPath);
    std::int32_t GetSelectedIndex(RE::IMenu* a_menu, const std::string& a_listPath);
    void SpeakText(const std::string& a_text, bool a_interrupt = false);
    std::string CleanLocalizationString(const std::string& a_text);
    void SpeakMessageBox(RE::IMenu* a_menu);
    bool IsMenuPanelVisible(RE::IMenu* a_menu, const std::string& a_panelPath);
    void TryAnnounceMenu(const std::string& a_menuName, const std::string& a_announcement, const std::string& a_listPath);

    // Menu tracking
    bool m_mainMenuOpen = false;
    bool m_pauseMenuOpen = false;
    bool m_messageBoxOpen = false;
    bool m_dialogueMenuOpen = false;
    std::int32_t m_lastMainMenuSelection = -1;
    std::int32_t m_lastPauseMenuSelection = -1;
    bool m_inputHandlerRegistered = false;
    bool m_mainMenuAnnounced = false;
    bool m_pauseMenuAnnounced = false;

    // List paths (discovered from log analysis)
    static constexpr const char* MAIN_MENU_LIST_PATH = "root.Menu_mc.MainPanel_mc.List_mc";
    static constexpr const char* PAUSE_MENU_LIST_PATH = "root.Menu_mc.MainPanel_mc.List_mc";
    static constexpr const char* MAIN_MENU_PANEL_PATH = "root.Menu_mc.MainPanel_mc";
    static constexpr const char* PAUSE_MENU_PANEL_PATH = "root.Menu_mc.MainPanel_mc";
};
