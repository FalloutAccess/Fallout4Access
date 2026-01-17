#pragma once

#include "pch.h"
#include "HUDNotificationAccessibility.h"
#include "PipBoyMenuAccessibility.h"
#include "ExamineMenuAccessibility.h"

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
    void SpeakText(const std::string& a_text, bool a_interrupt = false);
    void SpeakMessageBox(RE::IMenu* a_menu);
    void SpeakMessageBoxFromScaleform(RE::IMenu* a_menu);
    void ScheduleMessageBoxRead(int a_framesRemaining);

    // Menu tracking
    bool m_mainMenuOpen = false;
    bool m_pauseMenuOpen = false;
    bool m_messageBoxOpen = false;
    std::atomic<bool> m_messageBoxSpoken{ false };
    bool m_dialogueMenuOpen = false;
    bool m_specialMenuOpen = false;
    bool m_terminalMenuOpen = false;
    bool m_vatsMenuOpen = false;
    bool m_hudMenuOpen = false;
    bool m_pipBoyMenuOpen = false;
    bool m_examineMenuOpen = false;
    bool m_inputHandlerRegistered = false;
};
