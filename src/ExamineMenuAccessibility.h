#pragma once

#include "pch.h"
#include <RE/E/ExamineMenu.h>

// Handles ExamineMenu (weapon/armor workbench) accessibility
// Manages lifecycle and coordinates with ExamineMenuHook for state tracking
class ExamineMenuAccessibility
{
public:
    static ExamineMenuAccessibility* GetSingleton();

    void OnExamineMenuOpened();
    void OnExamineMenuClosed();
    void Reset();

private:
    ExamineMenuAccessibility() = default;
    ~ExamineMenuAccessibility() = default;
    ExamineMenuAccessibility(const ExamineMenuAccessibility&) = delete;
    ExamineMenuAccessibility(ExamineMenuAccessibility&&) = delete;
    ExamineMenuAccessibility& operator=(const ExamineMenuAccessibility&) = delete;
    ExamineMenuAccessibility& operator=(ExamineMenuAccessibility&&) = delete;

    void SpeakText(const std::string& a_text);
    std::string GetWorkbenchType(RE::ExamineMenu* a_menu);

    bool m_isOpen = false;
};
