#include "CustomTitleBar.h"
#include "AppWindow.h"
#include "TerminalThread.h"
#include "TranslationHelper.h"
#include "SettingsDialog.h"
#include <wx/simplebook.h>
#include <wx/display.h>
#include <algorithm>

CustomTitleBar::CustomTitleBar(wxWindow* parent, wxSimplebook* notebook)
    : wxPanel(parent, wxID_ANY), m_notebook(notebook), m_tabContainer(nullptr) {
    SetBackgroundColour(wxColour(30, 30, 30));
    
    // Calculate title bar height based on DPI scale
    double dpiScale = 1.0;
#ifdef _WIN32
    if (GetHandle()) {
        dpiScale = GetDPIScaleFactor();
    } else {
        // Fallback to display DPI if handle not available
        int screenNum = wxDisplay::GetFromWindow(this);
        if (screenNum != wxNOT_FOUND) {
            wxDisplay display(screenNum);
            int dpi = display.GetPPI().GetWidth();
            dpiScale = static_cast<double>(dpi) / 96.0;
        }
    }
    if (dpiScale <= 0.0) dpiScale = 1.0;
#endif
    
    int baseHeight = 50;
    int scaledHeight = static_cast<int>(baseHeight * dpiScale);
    SetMinSize(wxSize(-1, scaledHeight));
    
    int baseButtonSize = 30;
    int scaledButtonSize = static_cast<int>(baseButtonSize * dpiScale);
    int baseNewTabWidth = 40;
    int scaledNewTabWidth = static_cast<int>(baseNewTabWidth * dpiScale);

    m_drawerButton = new wxButton(this, wxID_ANY, wxString::FromUTF8("\u2630"), wxDefaultPosition, wxSize(scaledButtonSize, scaledButtonSize), wxBORDER_NONE);
    m_minimizeButton = new wxButton(this, wxID_ANY, "_", wxDefaultPosition, wxSize(scaledButtonSize, scaledButtonSize), wxBORDER_NONE);
    m_maximizeButton = new wxButton(this, wxID_ANY, "[]", wxDefaultPosition, wxSize(scaledButtonSize, scaledButtonSize), wxBORDER_NONE);
    m_closeButton = new wxButton(this, wxID_ANY, "X", wxDefaultPosition, wxSize(scaledButtonSize, scaledButtonSize), wxBORDER_NONE);
    m_newTabButton = new wxButton(this, wxID_ANY, "+", wxDefaultPosition, wxSize(scaledNewTabWidth, scaledButtonSize), wxBORDER_NONE);

    m_drawerButton->SetBackgroundColour(wxColour(60, 60, 60));
    m_drawerButton->SetForegroundColour(wxColour(255, 255, 255));
    m_minimizeButton->SetBackgroundColour(wxColour(60, 60, 60));
    m_minimizeButton->SetForegroundColour(wxColour(255, 255, 255));
    m_maximizeButton->SetBackgroundColour(wxColour(60, 60, 60));
    m_maximizeButton->SetForegroundColour(wxColour(255, 255, 255));
    m_closeButton->SetBackgroundColour(wxColour(255, 0, 0));
    m_closeButton->SetForegroundColour(wxColour(255, 255, 255));
    m_newTabButton->SetBackgroundColour(wxColour(0, 0, 0, 0));
    m_newTabButton->SetForegroundColour(wxColour(255, 255, 255));
    wxFont font = m_newTabButton->GetFont();
    font.SetWeight(wxFONTWEIGHT_BOLD);
    m_newTabButton->SetFont(font);

    m_titleText = new wxStaticText(this, wxID_ANY, TranslationHelper::Tr("oceanTerm"));
    m_titleText->SetForegroundColour(wxColour(255, 255, 255));
    wxFont titleFont = m_titleText->GetFont();
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    titleFont.SetStyle(wxFONTSTYLE_ITALIC);
    m_titleText->SetFont(titleFont);

    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_titleText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 20);

    // Tab container sizer (for tabs to be inserted here)
    m_tabContainer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_tabContainer, 0, wxALIGN_BOTTOM);

    wxBoxSizer* newTabSizer = new wxBoxSizer(wxVERTICAL);
    newTabSizer->Add(0, 2, 0, wxEXPAND);
    newTabSizer->Add(m_newTabButton, 0, wxALIGN_CENTER_HORIZONTAL);
    sizer->Add(newTabSizer, 0, wxALIGN_BOTTOM | wxLEFT, 8);
    sizer->AddStretchSpacer();
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    buttonSizer->Add(m_drawerButton, 0, wxALIGN_CENTER_VERTICAL);
    buttonSizer->Add(m_minimizeButton, 0, wxALIGN_CENTER_VERTICAL);
    buttonSizer->Add(m_maximizeButton, 0, wxALIGN_CENTER_VERTICAL);
    buttonSizer->Add(m_closeButton, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(buttonSizer, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    SetSizer(sizer);

    Bind(wxEVT_PAINT, &CustomTitleBar::OnPaint, this);
    Bind(wxEVT_ERASE_BACKGROUND, &CustomTitleBar::OnEraseBackground, this);
    Bind(wxEVT_LEFT_DOWN, &CustomTitleBar::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &CustomTitleBar::OnLeftUp, this);
    Bind(wxEVT_MOTION, &CustomTitleBar::OnMouseMove, this);
    Bind(wxEVT_SIZE, &CustomTitleBar::OnSize, this);
    m_minimizeButton->Bind(wxEVT_BUTTON, &CustomTitleBar::OnMinimize, this);
    m_maximizeButton->Bind(wxEVT_BUTTON, &CustomTitleBar::OnMaximize, this);
    m_closeButton->Bind(wxEVT_BUTTON, &CustomTitleBar::OnClose, this);
    m_newTabButton->Bind(wxEVT_BUTTON, &CustomTitleBar::OnNewTabClicked, this);
    m_drawerButton->Bind(wxEVT_BUTTON, &CustomTitleBar::OnDrawerClicked, this);
    Bind(wxEVT_TAB_CLOSE, &CustomTitleBar::OnTabClose, this);
    Bind(wxEVT_TAB_SELECTED, &CustomTitleBar::OnTabSelected, this);

    // Initialize max tab container width
    UpdateMaxTabContainerWidth();
}

void CustomTitleBar::UpdateMaxTabContainerWidth() {
    int totalWidth = GetClientSize().GetWidth();
    int titleWidth = m_titleText->GetSize().GetWidth() + 40;
    int newTabWidth = m_newTabButton->GetSize().GetWidth();
    int drawerWidth = m_drawerButton->GetSize().GetWidth();
    int minBtnWidth = m_minimizeButton->GetSize().GetWidth();
    int maxBtnWidth = m_maximizeButton->GetSize().GetWidth();
    int closeBtnWidth = m_closeButton->GetSize().GetWidth();
    int buttonGap = 5;
    int rightButtonsWidth = drawerWidth + minBtnWidth + maxBtnWidth + closeBtnWidth + buttonGap * 4;

    // Tab container max width = total width - title - new tab button - right buttons - margins
    m_maxTabContainerWidth = totalWidth - titleWidth - newTabWidth - rightButtonsWidth - 20;
}

int CustomTitleBar::CalcMaxVisibleTabs() const {
    int totalWidth = GetClientSize().GetWidth();
    int titleWidth = m_titleText->GetSize().GetWidth() + 40;

    // Get actual button sizes (they are DPI-scaled now)
    int newTabWidth = m_newTabButton->GetSize().GetWidth();
    int drawerWidth = m_drawerButton->GetSize().GetWidth();
    int minBtnWidth = m_minimizeButton->GetSize().GetWidth();
    int maxBtnWidth = m_maximizeButton->GetSize().GetWidth();
    int closeBtnWidth = m_closeButton->GetSize().GetWidth();

    int buttonGap = 5;
    int rightButtonsWidth = drawerWidth + minBtnWidth + maxBtnWidth + closeBtnWidth + buttonGap * 4;

    // Each tab has 3px margin on all sides (6px total per tab)
    int tabMargin = 6;
    int totalTabs = (int)m_tabs.size();
    if (totalTabs == 0) return 0;

    // Calculate tab width dynamically based on available space
    int availWidth = totalWidth - titleWidth - newTabWidth - rightButtonsWidth - 20;
    int tabWidth = (availWidth - tabMargin * totalTabs) / totalTabs;

    // Set minimum and maximum tab width
    int minTabWidth = 120;
    int maxTabWidth = 200;
#ifdef __APPLE__
    minTabWidth /= 2;
    maxTabWidth /= 2;
#endif
    tabWidth = std::max(minTabWidth, std::min(maxTabWidth, tabWidth));

    if (tabWidth <= 0) return 0;

    // Calculate max visible tabs based on available space
    int maxVisible = availWidth / (tabWidth + tabMargin);

    return std::max(1, maxVisible);
}

void CustomTitleBar::LayoutTabs() {
    if (m_tabs.empty()) return;

    int maxVisible = CalcMaxVisibleTabs();
    int totalTabs = (int)m_tabs.size();

    m_overflowIndices.clear();

    // Determine which tabs overflow
    for (int i = 0; i < totalTabs; ++i) {
        bool visible = (i < maxVisible);
        m_tabs[i]->Show(visible);
        if (!visible) {
            m_overflowIndices.push_back(i);
        }
    }

    Layout();
}

ConnectInfo* CustomTitleBar::GetLastTab() {
    return m_tabs.empty() ? nullptr : m_tabs.back();
}

int CustomTitleBar::CalculateTabWidth() const {
    int totalWidth = GetClientSize().GetWidth();
    int titleWidth = m_titleText->GetSize().GetWidth() + 40;
    
    // Get actual button sizes (they are DPI-scaled now)
    int newTabWidth = m_newTabButton->GetSize().GetWidth();
    int drawerWidth = m_drawerButton->GetSize().GetWidth();
    int minBtnWidth = m_minimizeButton->GetSize().GetWidth();
    int maxBtnWidth = m_maximizeButton->GetSize().GetWidth();
    int closeBtnWidth = m_closeButton->GetSize().GetWidth();
    
    int buttonGap = 5;
    int rightButtonsWidth = drawerWidth + minBtnWidth + maxBtnWidth + closeBtnWidth + buttonGap * 4;
    int availWidth = totalWidth - titleWidth - newTabWidth - rightButtonsWidth - 20;
    
    // Each tab has 3px margin on all sides (6px total per tab)
    int tabMargin = 6;
    int totalTabs = (int)m_tabs.size();
    if (totalTabs == 0) {
#ifdef __APPLE__
        return 60;
#else
        return 120;
#endif
    }
    
    // Calculate tab width dynamically based on available space
    int tabWidth = (availWidth - tabMargin * totalTabs) / totalTabs;
    
    // Set minimum and maximum tab width
    int minTabWidth = 120;
    int maxTabWidth = 200;
#ifdef __APPLE__
    minTabWidth /= 2;
    maxTabWidth /= 2;
#endif
    tabWidth = std::max(minTabWidth, std::min(maxTabWidth, tabWidth));
    
    return tabWidth;
}

ConnectInfo* CustomTitleBar::AddTab(const wxString& label, wxWindow* contentPanel, const DeviceConfig& deviceConfig, bool showCloseButton) {
    m_notebook->AddPage(contentPanel, label, true);
    ConnectInfo* newTab = new ConnectInfo(this, label, contentPanel, deviceConfig, showCloseButton);
    m_tabs.push_back(newTab);

    // Calculate dynamic tab width
    int tabWidth = CalculateTabWidth();
    newTab->SetMinSize(wxSize(tabWidth, newTab->GetMinSize().GetHeight()));

    int tabMargin = 6;

    // Calculate current width of all tabs in container
    int currentTabsWidth = 0;
    for (size_t i = 0; i < m_tabContainer->GetItemCount(); ++i) {
        wxWindow* item = m_tabContainer->GetItem(i)->GetWindow();
        if (item) {
            currentTabsWidth += item->GetSize().GetWidth() + tabMargin;
        }
    }

    // Calculate total width with new tab
    int totalTabsWidth = currentTabsWidth + tabWidth + tabMargin;

    // If overflow, replace last tab in container
    if (totalTabsWidth > m_maxTabContainerWidth && m_tabContainer->GetItemCount() > 0) {
        // Get last tab in container
        size_t lastIdx = m_tabContainer->GetItemCount() - 1;
        wxSizerItem* lastItem = m_tabContainer->GetItem(lastIdx);
        wxWindow* lastTabWindow = lastItem->GetWindow();

        // Find corresponding ConnectInfo in m_tabs
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            if (m_tabs[i] == lastTabWindow) {
                // Add to overflow list
                m_overflowIndices.push_back(i);
                // Remove from container
                m_tabContainer->Remove(lastIdx);
                lastTabWindow->Hide();
                break;
            }
        }
    }

    m_tabContainer->Add(newTab, 0, wxALIGN_BOTTOM | wxLEFT | wxRIGHT | wxTOP, 3);

    // 取消所有其他tab的高亮
    for (auto t : m_tabs) t->SetActive(false);
    // 高亮新添加的tab
    newTab->SetActive(true);

    Layout();
    return newTab;
}

void CustomTitleBar::OnSize(wxSizeEvent& event) {
    UpdateMaxTabContainerWidth();
    Layout();
    LayoutTabs();
    Refresh();

    // Explicitly refresh all buttons to ensure they are drawn
    m_drawerButton->Refresh();
    m_minimizeButton->Refresh();
    m_maximizeButton->Refresh();
    m_closeButton->Refresh();
    m_newTabButton->Refresh();

    event.Skip();
}

void CustomTitleBar::OnNewTabClicked(wxCommandEvent& event) {
    wxCommandEvent newTabEvent(wxEVT_MENU, wxID_NEW);
    wxPostEvent(GetParent(), newTabEvent);
}

void CustomTitleBar::OnDrawerClicked(wxCommandEvent& event) {
    wxMenu menu;
    menu.Append(ID_SETTINGS, TranslationHelper::Tr("settings"));
    menu.Append(ID_NEW_TERMINAL, TranslationHelper::Tr("deviceList"));

    Bind(wxEVT_MENU, &CustomTitleBar::OnSettings, this, ID_SETTINGS);
    Bind(wxEVT_MENU, &CustomTitleBar::OnNewTerminal, this, ID_NEW_TERMINAL);

    // Add overflow tabs to menu
    if (!m_overflowIndices.empty()) {
        menu.AppendSeparator();
        for (size_t i = 0; i < m_overflowIndices.size(); ++i) {
            int tabIdx = m_overflowIndices[i];
            int menuId = ID_OVERFLOW_TAB_BASE + tabIdx;
            wxString tabLabel = m_tabs[tabIdx]->GetDeviceConfig().name;
            if (tabLabel.empty()) tabLabel = wxString::Format("Tab %d", tabIdx + 1);
            menu.Append(menuId, tabLabel);
            Bind(wxEVT_MENU, &CustomTitleBar::OnOverflowTabClicked, this, menuId);
        }
    }

    PopupMenu(&menu);

    Unbind(wxEVT_MENU, &CustomTitleBar::OnSettings, this, ID_SETTINGS);
    Unbind(wxEVT_MENU, &CustomTitleBar::OnNewTerminal, this, ID_NEW_TERMINAL);
    for (size_t i = 0; i < m_overflowIndices.size(); ++i) {
        int menuId = ID_OVERFLOW_TAB_BASE + m_overflowIndices[i];
        Unbind(wxEVT_MENU, &CustomTitleBar::OnOverflowTabClicked, this, menuId);
    }
}

void CustomTitleBar::OnOverflowTabClicked(wxCommandEvent& event) {
    int tabIdx = event.GetId() - ID_OVERFLOW_TAB_BASE;
    if (tabIdx < 0 || tabIdx >= (int)m_tabs.size()) return;

    // Check if tab container has any tabs
    if (m_tabContainer->GetItemCount() == 0) {
        // No tabs in container, just add this one
        m_tabContainer->Add(m_tabs[tabIdx], 0, wxALIGN_BOTTOM | wxLEFT | wxRIGHT | wxTOP, 3);
        m_tabs[tabIdx]->Show();
        m_tabs[tabIdx]->SetActive(true);

        // Remove from overflow list
        auto it = std::find(m_overflowIndices.begin(), m_overflowIndices.end(), tabIdx);
        if (it != m_overflowIndices.end()) {
            m_overflowIndices.erase(it);
        }

        Layout();
        return;
    }

    // Get last tab in container
    size_t lastIdx = m_tabContainer->GetItemCount() - 1;
    wxSizerItem* lastItem = m_tabContainer->GetItem(lastIdx);
    wxWindow* lastTabWindow = lastItem->GetWindow();

    // Find corresponding ConnectInfo in m_tabs
    int lastTabIdx = -1;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i] == lastTabWindow) {
            lastTabIdx = i;
            break;
        }
    }

    if (lastTabIdx == -1) return;

    // Remove last tab from container and add to overflow
    m_tabContainer->Remove(lastIdx);
    lastTabWindow->Hide();
    m_overflowIndices.push_back(lastTabIdx);

    // Add selected tab to container
    m_tabContainer->Add(m_tabs[tabIdx], 0, wxALIGN_BOTTOM | wxLEFT | wxRIGHT | wxTOP, 3);
    m_tabs[tabIdx]->Show();
    m_tabs[tabIdx]->SetActive(true);

    // Remove selected tab from overflow list
    auto it = std::find(m_overflowIndices.begin(), m_overflowIndices.end(), tabIdx);
    if (it != m_overflowIndices.end()) {
        m_overflowIndices.erase(it);
    }

    Layout();
}

void CustomTitleBar::OnSettings(wxCommandEvent& event) {
    SettingsDialog dialog(this);
    if (dialog.ShowModal() == wxID_OK) {
        // Language changed, need to refresh UI
        // Send event to parent to refresh
        wxCommandEvent refreshEvent(wxEVT_COMMAND_MENU_SELECTED, wxID_REFRESH);
        wxPostEvent(GetParent(), refreshEvent);
    }
}

void CustomTitleBar::OnNewTerminal(wxCommandEvent& event) {
    wxCommandEvent newTabEvent(wxEVT_MENU, ID_NEW_TAB);
    wxPostEvent(GetParent(), newTabEvent);
}

void CustomTitleBar::OnTabClose(wxCommandEvent& event) {
    ConnectInfo* tab = (ConnectInfo*)event.GetEventObject();
    int tabIndex = -1;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i] == tab) {
            tabIndex = (int)i;
            break;
        }
    }

    if (tabIndex != -1) {
        m_notebook->RemovePage(m_notebook->FindPage(m_tabs[tabIndex]->GetContentPanel()));
        m_tabs.erase(m_tabs.begin() + tabIndex);
        m_tabContainer->Detach(tab);
        tab->Destroy();
        LayoutTabs();
    }
}

void CustomTitleBar::OnTabSelected(wxCommandEvent& event) {
    ConnectInfo* tab = (ConnectInfo*)event.GetEventObject();
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i] == tab) {
            // Find the notebook page index for this tab's content panel
            int notebookIndex = m_notebook->FindPage(m_tabs[i]->GetContentPanel());
            if (notebookIndex != wxNOT_FOUND) {
                m_notebook->SetSelection(notebookIndex);
            }
            for (auto t : m_tabs) t->SetActive(false);
            tab->SetActive(true);
            break;
        }
    }
}

void CustomTitleBar::NotifyAllTabsResize() {
    // 先Layout确保所有tab都被正确布局
    m_notebook->Layout();
    
    // 保存当前选中的页面
    int currentSelection = m_notebook->GetSelection();
    
    // 通知所有tab调整大小
    for (auto tab : m_tabs) {
        TerminalThread* thread = tab->GetTerminalThread();
        if (thread) {
            wxWindow* contentPanel = tab->GetContentPanel();
            if (contentPanel) {
                // 临时切换到这个tab以获取正确的size
                int pageIndex = m_notebook->FindPage(contentPanel);
                if (pageIndex != wxNOT_FOUND) {
                    m_notebook->SetSelection(pageIndex);
                    m_notebook->Layout();
                    
                    wxSize size = contentPanel->GetSize();
                    int cellWidth = 12;
                    int cellHeight = 24;
                    int availableHeight = size.GetHeight();
                    if (availableHeight < 100) availableHeight = 100;
                    int rows = availableHeight / cellHeight;
                    int cols = size.GetWidth() / cellWidth;
                    if (rows < 10) rows = 10;
                    if (cols < 40) cols = 40;
                    thread->ResizeVTerm(rows, cols);
                }
            }
        }
    }
    
    // 恢复原来的选中页面
    if (currentSelection != wxNOT_FOUND) {
        m_notebook->SetSelection(currentSelection);
    }
}

void CustomTitleBar::OnMinimize(wxCommandEvent& event) {
    ((wxFrame*)GetParent())->Iconize(true);
}

void CustomTitleBar::OnMaximize(wxCommandEvent& event) {
    wxFrame* frame = (wxFrame*)GetParent();
    
    if (frame->IsMaximized()) {
        frame->Maximize(false);
    } else {
        // Get screen geometry
        int screenNum = wxDisplay::GetFromWindow(frame);
        if (screenNum != wxNOT_FOUND) {
            wxDisplay display(screenNum);
            wxRect screenRect = display.GetClientArea();
            
            // Maximize to screen area (not including taskbar)
            frame->Maximize(true);
            
            // Ensure window doesn't exceed screen bounds
            wxSize windowSize = frame->GetSize();
            if (windowSize.GetWidth() > screenRect.width || windowSize.GetHeight() > screenRect.height) {
                frame->SetSize(screenRect.width, screenRect.height);
                frame->SetPosition(wxPoint(screenRect.x, screenRect.y));
            }
        } else {
            frame->Maximize(true);
        }
    }
    
    // Force layout recalculation to ensure notebook expands
    frame->Layout();
}

void CustomTitleBar::OnClose(wxCommandEvent& event) {
    GetParent()->Close();
}

void CustomTitleBar::OnPaint(wxPaintEvent& event) {
    wxPaintDC dc(this);
    
    // Get current window size dynamically
    wxSize clientSize = GetClientSize();
    
    // Draw background to fill the entire area
    dc.SetBackground(wxBrush(wxColour(30, 30, 30)));
    dc.Clear();
    
    event.Skip();
}

void CustomTitleBar::OnEraseBackground(wxEraseEvent& event) {
    // Do nothing - prevent system background erasing to avoid flickering and artifacts
    // Don't call event.Skip() to tell wxWidgets we handle background ourselves
}

void CustomTitleBar::OnLeftDown(wxMouseEvent& event) {
    // Only capture if not clicking on a button
    wxWindow* clickedWindow = wxDynamicCast(event.GetEventObject(), wxWindow);
    if (clickedWindow && (clickedWindow == m_drawerButton ||
                          clickedWindow == m_minimizeButton ||
                          clickedWindow == m_maximizeButton ||
                          clickedWindow == m_closeButton ||
                          clickedWindow == m_newTabButton)) {
        event.Skip();
        return;
    }

    // Capture mouse for dragging
    CaptureMouse();
    // Calculate offset: mouse position relative to window top-left corner
    wxPoint mouseScreenPos = wxGetMousePosition();
    wxPoint windowScreenPos = GetParent()->GetPosition();
    m_delta = mouseScreenPos - windowScreenPos;
}

void CustomTitleBar::OnLeftUp(wxMouseEvent& event) {
    if (HasCapture()) {
        ReleaseMouse();
    }
}

void CustomTitleBar::OnMouseMove(wxMouseEvent& event) {
    if (event.Dragging() && HasCapture()) {
        wxPoint currentMousePos = wxGetMousePosition();
        wxPoint newWindowPos = currentMousePos - m_delta;
        GetParent()->Move(newWindowPos);
    }
}
