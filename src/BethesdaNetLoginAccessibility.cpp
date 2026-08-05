#include "BethesdaNetLoginAccessibility.h"
#include "LocalizationHelper.h"
#include "ScaleformHelpers.h"
#include "SpeechManager.h"
#include <mutex>

namespace
{
	// Log loader-content resolution failures only once per session so we don't
	// spam fallout4access.log if the path drifts.
	std::once_flag g_loginRootFailureLogFlag;

	// Strip $ keys / underscores via the game's translator and trim whitespace.
	std::string Clean(const std::string& a_text)
	{
		std::string t = LocalizationHelper::Translate(a_text);
		auto        first = t.find_first_not_of(" \t\n\r");
		if (first == std::string::npos) {
			return "";
		}
		auto last = t.find_last_not_of(" \t\n\r");
		return t.substr(first, last - first + 1);
	}
}

BethesdaNetLoginAccessibility* BethesdaNetLoginAccessibility::GetSingleton()
{
	static BethesdaNetLoginAccessibility singleton;
	return &singleton;
}

bool BethesdaNetLoginAccessibility::GetLoginRoot(RE::MainMenu* a_menu, Scaleform::GFx::Value& a_out)
{
	if (!a_menu || !a_menu->uiMovie) {
		return false;
	}

	// Primary: LoginLoader_mc is a public Loader var, so its .content should
	// resolve directly to the Main_LoginHelper instance.
	if (a_menu->uiMovie->GetVariable(&a_out, LOGIN_CONTENT_PATH) && a_out.IsObject()) {
		// Validate it's actually the login movie by probing a known page clip.
		Scaleform::GFx::Value probe;
		if (sfh::GetObjectMember(a_out, "LoginPage_mc", probe) ||
			sfh::GetObjectMember(a_out, "NewAccountPage_mc", probe)) {
			return true;
		}
	}

	// Fallback: walk LoginHolder_mc -> getChildAt(0) (the Loader) ->
	// getChildAt(0) (the loaded content).
	Scaleform::GFx::Value holder;
	if (a_menu->uiMovie->GetVariable(&holder, LOGIN_HOLDER_PATH) && holder.IsObject()) {
		Scaleform::GFx::Value loader;
		if (sfh::GetChildAt(holder, 0, loader)) {
			Scaleform::GFx::Value content;
			// Loader.content first; fall back to its first display child.
			if (sfh::GetObjectMember(loader, "content", content) || sfh::GetChildAt(loader, 0, content)) {
				Scaleform::GFx::Value probe;
				if (sfh::GetObjectMember(content, "LoginPage_mc", probe) ||
					sfh::GetObjectMember(content, "NewAccountPage_mc", probe)) {
					a_out = content;
					return true;
				}
			}
		}
	}

	std::call_once(g_loginRootFailureLogFlag, []() {
		REX::INFO("BethesdaNetLoginAccessibility: failed to resolve Main_LoginHelper content; login announcements will silently bail");
	});
	return false;
}

BethesdaNetLoginAccessibility::Page BethesdaNetLoginAccessibility::DetectPage(Scaleform::GFx::Value& a_root)
{
	// Priority order: the spinner overlays everything and blocks input.
	if (sfh::ChildVisible(a_root, "Spinner_mc")) {
		return Page::Spinner;
	}
	if (sfh::ChildVisible(a_root, "EULAPage_mc")) {
		return Page::EULA;
	}
	if (sfh::ChildVisible(a_root, "LoginPage_mc")) {
		return Page::Login;
	}
	if (sfh::ChildVisible(a_root, "NewAccountPage_mc")) {
		return Page::NewAccount;
	}
	return Page::None;
}

std::string BethesdaNetLoginAccessibility::GetSpinnerText(Scaleform::GFx::Value& a_root)
{
	return Clean(sfh::ReadTextAtPath(a_root, { "Spinner_mc", "textField" }));
}

std::string BethesdaNetLoginAccessibility::GetError(Scaleform::GFx::Value& a_root, Page a_page)
{
	switch (a_page) {
		case Page::Login:
			return Clean(sfh::ReadTextAtPath(a_root, { "LoginPage_mc", "Error_tf" }));
		case Page::NewAccount:
			return Clean(sfh::ReadTextAtPath(a_root, { "NewAccountPage_mc", "Error_tf" }));
		default:
			return "";
	}
}

std::string BethesdaNetLoginAccessibility::BuildPageAnnouncement(Scaleform::GFx::Value& a_root, Page a_page)
{
	switch (a_page) {
		case Page::Spinner:
			return GetSpinnerText(a_root);

		case Page::NewAccount: {
			std::string header = Clean(sfh::ReadTextAtPath(a_root, { "NewAccountPage_mc", "LoginHeader_tf" }));
			std::string email = Clean(sfh::ReadTextAtPath(a_root, { "NewAccountPage_mc", "NewEmailInput_tf" }));
			std::string result = header.empty() ? "Create account" : header;
			result += email.empty() ? ". Email field, empty" : (". Email field: " + email);
			return result;
		}

		case Page::Login: {
			std::string header = Clean(sfh::ReadTextAtPath(a_root, { "LoginPage_mc", "LoginHeader_tf" }));
			std::string user = Clean(sfh::ReadTextAtPath(a_root, { "LoginPage_mc", "UsernameInput_tf" }));
			std::string result = header.empty() ? "Sign in" : header;
			result += user.empty() ? ". Username field, empty" : (". Username field: " + user);
			// Never read the masked password back; just announce its presence.
			result += ". Password field";
			return result;
		}

		case Page::EULA: {
			std::string title = Clean(sfh::ReadTextAtPath(a_root, { "EULAPage_mc", "Title_tf" }));
			std::string body = Clean(sfh::ReadTextAtPath(a_root, { "EULAPage_mc", "EULA_tf" }));
			std::string result = title;
			if (!body.empty()) {
				result += result.empty() ? body : (". " + body);
			}
			return result;
		}

		default:
			return "";
	}
}

void BethesdaNetLoginAccessibility::OnOpened()
{
	REX::INFO("BethesdaNetLoginAccessibility: login state opened");
	m_isOpen = true;
	m_lastPage = Page::None;
	m_lastError.clear();
	m_lastSpinnerText.clear();
	m_lastSpokenText = "Bethesda dot net sign in";
	SpeakText(m_lastSpokenText);
}

void BethesdaNetLoginAccessibility::Reset()
{
	m_isOpen = false;
	m_lastPage = Page::None;
	m_lastError.clear();
	m_lastSpinnerText.clear();
	m_lastSpokenText.clear();
}

void BethesdaNetLoginAccessibility::CheckState(RE::MainMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return;
	}

	Scaleform::GFx::Value loginRoot;
	if (!GetLoginRoot(a_menu, loginRoot)) {
		return;
	}

	Page currentPage = DetectPage(loginRoot);

	// The spinner blocks input; announce its text once on appearance/change and
	// suppress the rest of the polling while it is up.
	if (currentPage == Page::Spinner) {
		std::string spinnerText = GetSpinnerText(loginRoot);
		if (spinnerText != m_lastSpinnerText) {
			m_lastSpinnerText = spinnerText;
			if (!spinnerText.empty()) {
				SpeakText(spinnerText);
			}
		}
		m_lastPage = currentPage;
		return;
	}
	m_lastSpinnerText.clear();

	// On page change, announce the page's header and field summary.
	if (currentPage != m_lastPage) {
		m_lastPage = currentPage;
		m_lastError.clear();
		std::string announcement = BuildPageAnnouncement(loginRoot, currentPage);
		if (!announcement.empty() && announcement != m_lastSpokenText) {
			m_lastSpokenText = announcement;
			SpeakText(announcement);
		}
		return;
	}

	// Same page: announce error messages as they appear/change.
	std::string error = GetError(loginRoot, currentPage);
	if (error != m_lastError) {
		m_lastError = error;
		if (!error.empty()) {
			SpeakText(error);
		}
	}
}

void BethesdaNetLoginAccessibility::SpeakText(const std::string& a_text)
{
	if (a_text.empty()) {
		return;
	}
	SpeechManager::GetSingleton()->Speak(a_text);
}
