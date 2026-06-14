#pragma once

#include <wx/wx.h>
#include "CustomTitleBar.h"
#include <memory>
#include <vector>
#include <uv.h>
//#include "TerminalTab.h"

enum {
    ID_QUIT = wxID_EXIT,
    ID_ABOUT = wxID_ABOUT,
    ID_NEW_TAB = wxID_HIGHEST + 1
};

class wxSimplebook;

class AppWindow : public wxFrame {
public:
    AppWindow(const wxString& title, const wxPoint& pos, const wxSize& size);
    virtual ~AppWindow();

    wxSimplebook* GetNotebook() const { return m_notebook; }

private:
    CustomTitleBar* m_titleBar;
    wxSimplebook* m_notebook;
    void OnQuit(wxCommandEvent& event);
    void OnNewTab(wxCommandEvent& event);
    void OnIdle(wxIdleEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnDeviceOpenRequest(wxCommandEvent& event);
    void OnDeviceDeleteRequest(wxCommandEvent& event);
    
    void CreateDashboardTab();
    void CreateTerminalTab(const DeviceConfig& device);
    //std::vector<std::shared_ptr<TerminalTab>> terminal_tabs_;
    
    wxDECLARE_EVENT_TABLE();

#if defined(__WXMSW__)
    bool MSWHandleMessage(WXLRESULT* result, WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif
};

