#pragma once

#include <wx/wx.h>
#include "ConnectInfo.h"
#include <wx/simplebook.h>
#include <vector>

enum {
    ID_SETTINGS = wxID_HIGHEST + 1,
    ID_NEW_TERMINAL,
    ID_OVERFLOW_TAB_BASE
};

class wxSimplebook;

class CustomTitleBar : public wxPanel {
public:
    CustomTitleBar(wxWindow* parent, wxSimplebook* notebook, wxWindow* appWindow = nullptr);
    ConnectInfo* AddTab(const wxString& label, wxWindow* contentPanel, const DeviceConfig& deviceConfig, bool showCloseButton = true, bool isLocalTerminal = false);
    ConnectInfo* GetLastTab();
    void NotifyAllTabsResize();

private:
    void OnPaint(wxPaintEvent& event);
    void OnEraseBackground(wxEraseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnClose(wxCommandEvent& event);
    void OnMaximize(wxCommandEvent& event);
    void OnMinimize(wxCommandEvent& event);
    void OnDrawerClicked(wxCommandEvent& event);
    void OnSettings(wxCommandEvent& event);
    void OnNewTerminal(wxCommandEvent& event);
    void OnOverflowTabClicked(wxCommandEvent& event);
    void OnNewTabClicked(wxCommandEvent& event);
    void OnTabClose(wxCommandEvent& event);
    void OnTabSelected(wxCommandEvent& event);

    void LayoutTabs();
    int CalcMaxVisibleTabs() const;
    int CalculateTabWidth() const;
    void UpdateMaxTabContainerWidth();

    wxPoint m_delta;
    wxStaticText* m_titleText;
    wxSimplebook* m_notebook;
    wxBoxSizer* m_tabContainer;
    wxButton* m_drawerButton;
    wxButton* m_minimizeButton;
    wxButton* m_maximizeButton;
    wxButton* m_closeButton;
    wxButton* m_newTabButton;
    std::vector<ConnectInfo*> m_tabs;
    std::vector<int> m_overflowIndices;
    int m_maxTabContainerWidth;
    wxWindow* m_appWindow;
};
