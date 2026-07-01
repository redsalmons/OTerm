#include "SplitterPanel.h"
#include "TerminalPanel.h"
#include <wx/splitter.h>

SplitterPanel::SplitterPanel(wxWindow* parent, TerminalPanel* originalPanel, bool horizontal)
    : wxPanel(parent, wxID_ANY),
      m_splitter(nullptr),
      m_panel1(originalPanel),
      m_panel2(nullptr) {
    
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    SetSizer(sizer);

    // Create splitter window
    m_splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                      wxSP_LIVE_UPDATE | wxSP_NOBORDER);
    sizer->Add(m_splitter, 1, wxEXPAND);

    // Reparent the original panel to the splitter
    m_panel1->Reparent(m_splitter);

    // Create a new panel for the other side
    m_panel2 = new TerminalPanel(m_splitter, std::make_unique<LocalTerminalContainer>(24, 80, ""));

    // Split the window
    if (horizontal) {
        m_splitter->SplitHorizontally(m_panel1, m_panel2);
    } else {
        m_splitter->SplitVertically(m_panel1, m_panel2);
    }

    Layout();
}

SplitterPanel::~SplitterPanel() {
}

void SplitterPanel::StopThreads() {
    if (m_panel1) {
        m_panel1->StopThreads();
    }
    if (m_panel2) {
        m_panel2->StopThreads();
    }
}
