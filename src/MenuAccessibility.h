#pragma once

#include "pch.h"
#include "HUDNotificationAccessibility.h"
#include "PipBoyMenuAccessibility.h"
#include "ExamineMenuAccessibility.h"
#include "BarterMenuAccessibility.h"
#include "ContainerMenuAccessibility.h"
#include "LoadingMenuHook.h"
#include "WorkshopMenuAccessibility.h"
#include "WorkshopCaravanMenuAccessibility.h"

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

    // Menu tracking
    bool m_mainMenuOpen = false;
    bool m_pauseMenuOpen = false;
    bool m_messageBoxOpen = false;
    bool m_dialogueMenuOpen = false;
    bool m_specialMenuOpen = false;
    bool m_terminalMenuOpen = false;
    bool m_vatsMenuOpen = false;
    bool m_hudMenuOpen = false;
    bool m_pipBoyMenuOpen = false;
    bool m_examineMenuOpen = false;
    bool m_powerArmorModMenuOpen = false;
    bool m_cookingMenuOpen = false;
    bool m_robotModMenuOpen = false;
    bool m_containerMenuOpen = false;
    bool m_barterMenuOpen = false;
    bool m_levelUpMenuOpen = false;
    bool m_loadingMenuOpen = false;
    bool m_workshopMenuOpen = false;
    bool m_caravanMenuOpen = false;
    bool m_inputHandlerRegistered = false;
};
