#pragma once
#include "../../../SDK/SDK.h"

#ifndef TEXTMODE
#include "../Render.h"
#include <ImGui/TextEditor.h>

struct Output_t
{
	std::string m_sFunction;
	std::string m_sLog;
	size_t m_iID;

	Color_t tAccent;
};

class CMenu
{
private:
	void DrawMenu();

	void MenuAimbot(int iTab);
	void MenuVisuals(int iTab);
	void MenuHvH(int iTab);
	void MenuMisc(int iTab);
	void MenuAnticheat(int iTab);
	void MenuLogs(int iTab);
	void MenuSettings(int iTab);
	void MenuSearch(std::string sSearch);

	void AddDraggable(const char* sLabel, ConfigVar<DragBox_t>& tVar, bool bShouldDraw = true, ImVec2 vSize = { H::Draw.Scale(100), H::Draw.Scale(40) });
	void AddResizableDraggable(const char* sLabel, ConfigVar<WindowBox_t>& tVar, bool bShouldDraw = true, ImGuiSizeCallback fCustomCallback = nullptr, ImVec2 vMinSize = { H::Draw.Scale(100), H::Draw.Scale(100) }, ImVec2 vMaxSize = { H::Draw.Scale(1000), H::Draw.Scale(1000) });
	void DrawBinds();

	std::deque<Output_t> m_vOutput = {};
	size_t m_iMaxOutputSize = 1000;

public:
	void Render();
	void AddOutput(const char* sFunction, const char* sLog, Color_t tColor = Vars::Menu::Theme::Accent.Value);
	void ShowNotification(const char* sTitle, const char* sMessage);
	void ShowDeferredNotification(const char* sTitle, const char* sMessage);
	void ProcessDeferredNotifications();
	void DrawNotifications();

	bool m_bIsOpen = false;
	bool m_bInKeybind = false;
	bool m_bWindowHovered = false;

	struct Notification_t
	{
		std::string m_sTitle;
		std::string m_sMessage;
		bool m_bVisible;
	};
	std::vector<Notification_t> m_vNotifications;
	std::vector<Notification_t> m_vDeferredNotifications;
};
#else
class CMenu
{
public:
	void Render() {}
	void AddOutput(const char*, const char*, Color_t = {}) {}
	void ShowNotification(const char*, const char*) {}
	void ShowDeferredNotification(const char*, const char*) {}
	void ProcessDeferredNotifications() {}
	void DrawNotifications() {}
	bool m_bIsOpen = false;
	bool m_bInKeybind = false;
	bool m_bWindowHovered = false;
};
#endif // !TEXTMODE

ADD_FEATURE(CMenu, Menu);
