#ifndef SPLITTER_PANEL_H
#define SPLITTER_PANEL_H

#include <wx/wx.h>
#include <wx/splitter.h>

class TerminalPanel;

class SplitterPanel : public wxPanel {
public:
    SplitterPanel(wxWindow* parent, TerminalPanel* originalPanel, bool horizontal);
    ~SplitterPanel();

    void StopThreads();

private:
    wxSplitterWindow* m_splitter;
    TerminalPanel* m_panel1;
    TerminalPanel* m_panel2;
};

#endif
