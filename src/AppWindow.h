#pragma once

#include <wx/wx.h>
#include <wx/timer.h>
#include "CustomTitleBar.h"
#include <memory>
#include <vector>
#include <queue>
//#include "TerminalTab.h"

enum {
    ID_QUIT = wxID_EXIT,
    ID_ABOUT = wxID_ABOUT,
    ID_NEW_TAB = wxID_HIGHEST + 1
};

class wxSimplebook;

wxDECLARE_EVENT(wxEVT_SSH_DIRECT_CONNECT, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_DEVICE_SHOW_REQUEST, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_CREATE_LOCAL_TAB, wxCommandEvent);

class AppWindow : public wxFrame {
public:
    AppWindow(const wxString& title, const wxPoint& pos, const wxSize& size);
    virtual ~AppWindow();

    wxSimplebook* GetNotebook() const { return m_notebook; }

private:
    CustomTitleBar* m_titleBar = nullptr;
    wxSimplebook* m_notebook = nullptr;
    
    void OnQuit(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);
    void OnNewTab(wxCommandEvent& event);
    void OnNewLocalTerminal(wxCommandEvent& event);
    void OnIdle(wxIdleEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnDeviceOpenRequest(wxCommandEvent& event);
    void OnDeviceDeleteRequest(wxCommandEvent& event);
    void OnDeviceListUpdate(wxCommandEvent& event);
    void OnSSHDirectConnect(wxCommandEvent& event);
    void OnDeviceShowRequest(wxCommandEvent& event);
    void OnFileTransferRequest(wxCommandEvent& event);
    void OnCreateLocalTab(wxCommandEvent& event);
    
    // Timer for delayed dialog showing to fix Windows x86 unresponsiveness
    wxTimer m_deviceShowTimer;
    TerminalPanel* m_pendingDeviceShowPanel;
    void OnDeviceShowTimer(wxTimerEvent& event);

    // Appearance tracking: wxEVT_SYS_COLOUR_CHANGED is not reliably delivered
    // on wxMSW for dark-mode toggles, so the system state is also polled.
    wxTimer m_appearanceTimer;
    bool m_appWasDark = false;
    bool m_retheming = false;
    void RethemeChrome();
    
    void CreateDashboardTab();
    void CreateTerminalTab(const DeviceConfig& device);
    void CreateLocalTerminalTab();
    
    static int s_globalTabCounter;
    //std::vector<std::shared_ptr<TerminalTab>> terminal_tabs_;
    
    wxDECLARE_EVENT_TABLE();

#if defined(__WXMSW__)
    bool MSWHandleMessage(WXLRESULT* result, WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif
};

