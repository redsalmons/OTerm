#include "CustomTitleBar.h"
#include "AppWindow.h"
#include "TerminalThread.h"
#include "TermGLCanvas.h"
#include "TranslationHelper.h"
#include "SettingsDialog.h"
#include "GlobalConfig.h"
#include <wx/simplebook.h>
#include <wx/display.h>
#include <algorithm>

CustomTitleBar::CustomTitleBar(wxWindow* parent, wxSimplebook* notebook, wxWindow* appWindow)
    : wxPanel(parent, wxID_ANY), m_notebook(notebook), m_tabContainer(nullptr), m_appWindow(appWindow) {
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
    
    int baseHeight = 32;
#ifdef __APPLE__
    int scaledHeight = 32;
#else
    int scaledHeight = static_cast<int>(baseHeight * dpiScale);
#endif
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

void CustomTitleBar::LayoutTabs() {
    if (m_tabs.empty()) return;

    // Resync sizer order to match m_tabs order (tabs may have been reordered)
    // Detach all from sizer then re-add in m_tabs order
    {
        std::vector<ConnectInfo*> inSizer;
        for (size_t i = 0; i < m_tabContainer->GetItemCount(); ++i) {
            wxSizerItem* item = m_tabContainer->GetItem(i);
            if (item && item->GetWindow()) {
                inSizer.push_back(static_cast<ConnectInfo*>(item->GetWindow()));
            }
        }
        // Check if order already matches
        bool orderOk = (inSizer.size() == m_tabs.size());
        for (size_t i = 0; orderOk && i < m_tabs.size(); ++i) {
            if (inSizer[i] != m_tabs[i]) orderOk = false;
        }
        if (!orderOk) {
            // Detach all, re-add in correct order
            for (auto tab : inSizer) {
                m_tabContainer->Detach(tab);
            }
            for (auto tab : m_tabs) {
                m_tabContainer->Add(tab, 0, wxALIGN_BOTTOM | wxLEFT | wxRIGHT | wxTOP, 3);
            }
        }
    }

    UpdateMaxTabContainerWidth();
    int availWidth = m_maxTabContainerWidth;

    // Minimum tab width
    int minTabWidth = 80;

    int tabMargin = 6; // 3px left + 3px right margin on each tab sizer item
    
    // Refresh each tab's cached width, then accumulate to find how many fit
    std::vector<int> preferredWidths;
    for (auto tab : m_tabs) {
        preferredWidths.push_back(tab->GetPreferredWidth()); // also updates m_cachedWidth
    }

    // Determine how many tabs can be visible:
    // accumulate preferred widths; when adding the next tab would exceed availWidth
    // (even at minTabWidth), that tab and everything after overflows.
    int maxVisible = 0;
    int currentWidthSum = 0;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        int w = preferredWidths[i] + tabMargin;
        int minW = minTabWidth + tabMargin;
        // Use the larger of preferred and min to avoid undercount
        int needed = std::max(w, minW);
        if (currentWidthSum + needed <= availWidth) {
            currentWidthSum += needed;
            maxVisible = i + 1;
        } else {
            break;
        }
    }
    if (maxVisible == 0) maxVisible = 1; // Ensure at least 1 tab is visible

    int totalTabs = (int)m_tabs.size();
    m_overflowIndices.clear();

    // Show/hide tabs based on visibility
    for (int i = 0; i < totalTabs; ++i) {
        bool visible = (i < maxVisible);
        m_tabs[i]->Show(visible);
        if (!visible) {
            m_overflowIndices.push_back(i);
        }
    }

    // Now, calculate the actual widths for visible tabs
    int visibleRequiredWidth = 0;
    for (int i = 0; i < maxVisible; ++i) {
        visibleRequiredWidth += preferredWidths[i] + tabMargin;
    }

    if (visibleRequiredWidth <= availWidth) {
        // There is enough space! Draw each visible tab at its ideal preferred width
        for (int i = 0; i < maxVisible; ++i) {
            int tabWidth = preferredWidths[i];
            m_tabs[i]->SetMinSize(wxSize(tabWidth, m_tabs[i]->GetMinSize().GetHeight()));
            m_tabs[i]->SetSize(wxSize(tabWidth, m_tabs[i]->GetSize().GetHeight()));
        }
    } else {
        // Not enough space to draw all visible tabs at preferred widths.
        // Shrink them proportionally down to minTabWidth!
        int totalMinRequired = minTabWidth * maxVisible + tabMargin * maxVisible;
        int remainingSpace = availWidth - totalMinRequired;
        if (remainingSpace < 0) remainingSpace = 0;

        int totalPreferredSlack = 0;
        for (int i = 0; i < maxVisible; ++i) {
            totalPreferredSlack += (preferredWidths[i] - minTabWidth);
        }

        float scale = (totalPreferredSlack > 0) ? (float)remainingSpace / totalPreferredSlack : 0.0f;
        if (scale > 1.0f) scale = 1.0f;

        for (int i = 0; i < maxVisible; ++i) {
            int tabWidth = minTabWidth + static_cast<int>((preferredWidths[i] - minTabWidth) * scale);
            m_tabs[i]->SetMinSize(wxSize(tabWidth, m_tabs[i]->GetMinSize().GetHeight()));
            m_tabs[i]->SetSize(wxSize(tabWidth, m_tabs[i]->GetSize().GetHeight()));
        }
    }

    // Force layout update on the container
    m_tabContainer->Layout();
    Layout();
}

ConnectInfo* CustomTitleBar::GetLastTab() {
    return m_tabs.empty() ? nullptr : m_tabs.back();
}

ConnectInfo* CustomTitleBar::AddTab(const wxString& label, wxWindow* contentPanel, const DeviceConfig& deviceConfig, bool showCloseButton, bool isLocalTerminal) {
    m_notebook->AddPage(contentPanel, label, true);
    ConnectInfo* newTab = new ConnectInfo(this, label, contentPanel, deviceConfig, showCloseButton, isLocalTerminal);

    // Check if there is space for one more visible tab using accumulated preferred widths
    UpdateMaxTabContainerWidth();
    int availWidth = m_maxTabContainerWidth;
    int tabMargin = 6;

    // Sum up preferred widths of currently visible tabs
    int usedWidth = 0;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        bool isOverflow = std::find(m_overflowIndices.begin(), m_overflowIndices.end(), (int)i) != m_overflowIndices.end();
        if (!isOverflow) {
            int w = m_tabs[i]->GetCachedWidth();
            if (w <= 0) w = m_tabs[i]->GetPreferredWidth();
            usedWidth += w + tabMargin;
        }
    }
    int newTabWidth = newTab->GetPreferredWidth();

    if (usedWidth + newTabWidth + tabMargin <= availWidth) {
        // Enough space: append new tab at the end
        m_tabs.push_back(newTab);
    } else {
        // No space: insert new tab before the last visible tab so LayoutTabs
        // will show it and push the last visible tab into overflow
        int lastVisibleIdx = -1;
        for (int i = (int)m_tabs.size() - 1; i >= 0; --i) {
            bool isOverflow = std::find(m_overflowIndices.begin(), m_overflowIndices.end(), i) != m_overflowIndices.end();
            if (!isOverflow) {
                lastVisibleIdx = i;
                break;
            }
        }
        if (lastVisibleIdx != -1) {
            m_tabs.insert(m_tabs.begin() + lastVisibleIdx, newTab);
        } else {
            m_tabs.push_back(newTab);
        }
    }

    // Always add to sizer — LayoutTabs controls Show/Hide
    m_tabContainer->Add(newTab, 0, wxALIGN_BOTTOM | wxLEFT | wxRIGHT | wxTOP, 3);

    // 取消所有其他tab的高亮
    for (auto t : m_tabs) t->SetActive(false);
    // 高亮新添加的tab
    newTab->SetActive(true);

    LayoutTabs();
    Refresh();

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

    // Build device list submenu: "Open device list" entry + hidden (overflow) tabs
    wxMenu* deviceSubMenu = new wxMenu();
    deviceSubMenu->Append(ID_NEW_TERMINAL, TranslationHelper::Tr("deviceList"));

    if (!m_overflowIndices.empty()) {
        deviceSubMenu->AppendSeparator();
        for (size_t i = 0; i < m_overflowIndices.size(); ++i) {
            int tabIdx = m_overflowIndices[i];
            int menuId = ID_OVERFLOW_TAB_BASE + tabIdx;
            wxString tabLabel = wxString::FromUTF8(m_tabs[tabIdx]->GetDeviceConfig().name.c_str());
            if (tabLabel.empty()) tabLabel = wxString::Format("Tab %d", tabIdx + 1);
            deviceSubMenu->Append(menuId, tabLabel);
            Bind(wxEVT_MENU, &CustomTitleBar::OnOverflowTabClicked, this, menuId);
        }
    }

    // Attach the submenu to the drawer menu
    menu.AppendSubMenu(deviceSubMenu, TranslationHelper::Tr("deviceList"));

    Bind(wxEVT_MENU, &CustomTitleBar::OnSettings, this, ID_SETTINGS);
    Bind(wxEVT_MENU, &CustomTitleBar::OnNewTerminal, this, ID_NEW_TERMINAL);

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

    ConnectInfo* selectedTab = m_tabs[tabIdx];

    // Find the last currently visible tab index
    int lastVisibleIdx = -1;
    for (int i = (int)m_tabs.size() - 1; i >= 0; --i) {
        bool isOverflow = std::find(m_overflowIndices.begin(), m_overflowIndices.end(), i) != m_overflowIndices.end();
        if (!isOverflow) {
            lastVisibleIdx = i;
            break;
        }
    }

    // Move selected tab to the position of the last visible tab in m_tabs,
    // so LayoutTabs will render it visible and push the old last visible tab to overflow
    m_tabs.erase(m_tabs.begin() + tabIdx);
    int insertAt = (lastVisibleIdx != -1) ? lastVisibleIdx : (int)m_tabs.size();
    if (insertAt > (int)m_tabs.size()) insertAt = (int)m_tabs.size();
    m_tabs.insert(m_tabs.begin() + insertAt, selectedTab);

    // Activate selected tab and deactivate all others
    for (auto t : m_tabs) t->SetActive(false);
    selectedTab->SetActive(true);

    // Switch notebook to the selected tab's content panel
    int notebookIndex = m_notebook->FindPage(selectedTab->GetContentPanel());
    if (notebookIndex != wxNOT_FOUND) {
        m_notebook->SetSelection(notebookIndex);
    }

    // Show IME input box for new active tab
    TermGLCanvas* canvas = selectedTab->GetCanvas();
    if (canvas) {
        canvas->ShowIMEInputBox();
    }

    LayoutTabs();
    Refresh();
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
    int currentActiveIndex = -1;

    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i] == tab) {
            tabIndex = (int)i;
        }
        if (m_tabs[i]->IsActive()) {
            currentActiveIndex = (int)i;
        }
    }

    if (tabIndex != -1) {
        wxWindow* contentPanel = m_tabs[tabIndex]->GetContentPanel();
        int notebookPage = m_notebook->FindPage(contentPanel);

        // Check if we're closing the currently active tab
        bool closingActiveTab = (tabIndex == currentActiveIndex);

        // Determine which tab to switch to next if closing the active tab
        int nextTabIndex = -1;
        if (closingActiveTab && m_tabs.size() > 1) {
            // Try to switch to the next tab, or the previous tab if this is the last one
            if (tabIndex < (int)m_tabs.size() - 1) {
                nextTabIndex = tabIndex; // Next tab will be at same index after erase
            } else if (tabIndex > 0) {
                nextTabIndex = tabIndex - 1; // Previous tab
            }
        }

        // Switch to next tab BEFORE removing the current page (only if closing active tab)
        if (closingActiveTab) {
            if (nextTabIndex != -1 && nextTabIndex < (int)m_tabs.size()) {
                wxWindow* nextContentPanel = m_tabs[nextTabIndex]->GetContentPanel();
                int nextNotebookPage = m_notebook->FindPage(nextContentPanel);
                if (nextNotebookPage != wxNOT_FOUND) {
                    m_notebook->SetSelection(nextNotebookPage);
                }
            } else if (m_tabs.size() > 0) {
                // Only homepage tab remains, switch to it
                wxWindow* homeContentPanel = m_tabs[0]->GetContentPanel();
                int homeNotebookPage = m_notebook->FindPage(homeContentPanel);
                if (homeNotebookPage != wxNOT_FOUND) {
                    m_notebook->SetSelection(homeNotebookPage);
                }
            } else {
                // No tabs at all, show first page in notebook (should be homepage)
                if (m_notebook->GetPageCount() > 0) {
                    m_notebook->SetSelection(0);
                }
            }
        }

        // Remove the page
        if (notebookPage != wxNOT_FOUND) {
            m_notebook->RemovePage(notebookPage);
        }

        m_tabs.erase(m_tabs.begin() + tabIndex);
        m_tabContainer->Detach(tab);
        tab->Destroy();

        // Update tab activation states
        if (closingActiveTab) {
            if (nextTabIndex != -1 && nextTabIndex < (int)m_tabs.size()) {
                for (size_t i = 0; i < m_tabs.size(); ++i) {
                    m_tabs[i]->SetActive(i == (size_t)nextTabIndex);
                }
            } else if (m_tabs.size() > 0) {
                m_tabs[0]->SetActive(true);
            }
        } else {
            // Keep the current active tab active, adjust its index if needed
            if (currentActiveIndex != -1 && currentActiveIndex < (int)m_tabs.size()) {
                for (size_t i = 0; i < m_tabs.size(); ++i) {
                    m_tabs[i]->SetActive(i == (size_t)currentActiveIndex);
                }
            } else if (m_tabs.size() > 0) {
                // If the active tab was after the closed tab, its index decreased
                if (currentActiveIndex > tabIndex) {
                    currentActiveIndex--;
                }
                if (currentActiveIndex >= 0 && currentActiveIndex < (int)m_tabs.size()) {
                    for (size_t i = 0; i < m_tabs.size(); ++i) {
                        m_tabs[i]->SetActive(i == (size_t)currentActiveIndex);
                    }
                }
            }
        }

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
            
            // Show IME input box when tab is selected
            if (m_appWindow) {
                TermGLCanvas* canvas = tab->GetCanvas();
                if (canvas) {
                    canvas->ShowIMEInputBox();
                }
            }
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
            TermGLCanvas* canvas = tab->GetCanvas();
            if (canvas) {
                // 临时切换到这个tab以获取正确的size
                int pageIndex = m_notebook->FindPage(canvas);
                if (pageIndex != wxNOT_FOUND) {
                    m_notebook->SetSelection(pageIndex);
                    m_notebook->Layout();
                    
                    wxSize size = canvas->GetSize();
                    
                    // Get configured font size
                    int fontSize = GlobalConfig::GetFontSize();
                    if (fontSize == 0) fontSize = 12;

                    float dpiScale = canvas->GetDPIScale();
                    int terminalFontSize = fontSize;
                    if (dpiScale > 1.0f) {
                        terminalFontSize = static_cast<int>(fontSize * 2);
                    }
                    if (terminalFontSize < 8) terminalFontSize = 8;
                    if (terminalFontSize > 72) terminalFontSize = 72;

                    // Calculate cell size based on actual canvas metrics or fallback
                    int cellWidth = (canvas->m_cellWidth > 0) ? canvas->m_cellWidth : (terminalFontSize / 2);
                    int cellHeight = (canvas->m_cellHeight > 0) ? canvas->m_cellHeight : terminalFontSize;

                    if (cellWidth < 6) cellWidth = 6;
                    if (cellHeight < 12) cellHeight = 12;

                    // Account for margins (8px left/right, 4px top/bottom, DPI-scaled)
                    int margin_x = static_cast<int>(8 * dpiScale);
                    int margin_y = static_cast<int>(4 * dpiScale);

                    int availableHeight = size.GetHeight() - margin_y * 2;
                    int availableWidth = size.GetWidth() - margin_x * 2;
                    if (availableHeight < 100) availableHeight = 100;
                    if (availableWidth < 100) availableWidth = 100;

                    int rows = availableHeight / cellHeight;
                    int cols = availableWidth / cellWidth;
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
    
    if (frame->IsFullScreen()) {
        frame->ShowFullScreen(false);
    } else {
        frame->ShowFullScreen(true);
    }
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
