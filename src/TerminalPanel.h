#ifndef TERMINAL_PANEL_H
#define TERMINAL_PANEL_H

#include <wx/wx.h>
#include <wx/splitter.h>
#include "LocalTerminalContainer.h"
#include "TermGLCanvas.h"
#include "EventProxy.h"

class InfiniteSplitter;

class TerminalPanel : public wxPanel {
public:
    TerminalPanel(wxWindow* parent, LocalTerminalContainer* container = nullptr);
    ~TerminalPanel();
    
    LocalTerminalContainer* GetTerminalContainer() const { return m_terminalContainer; }
    void SetTerminalContainer(LocalTerminalContainer* container);
    
    TermGLCanvas* GetCanvas() const { return m_canvas; }
    void SetCanvas(TermGLCanvas* canvas);
    
    void ShowContextMenu();
    bool HasTerminal() const { return m_terminalContainer != nullptr; }
    
    // Called when this panel becomes the active rendering target
    void Activate();
    // Called when this panel is no longer the active rendering target
    void Deactivate();
    
    // Public split interface
    void DoSplit(wxSplitMode mode);
    
    // Get EventProxy for canvas communication
    EventProxyPtr GetEventProxy() const { return m_eventProxy; }

private:
    void OnClosePanel(wxCommandEvent& event);
    void OnTerminalDamage(wxThreadEvent& event);

    void UpdateCanvasFromTerminal();
    void SetupCanvasConnection();

    LocalTerminalContainer* m_terminalContainer;
    TermGLCanvas* m_canvas;
    wxStaticText* m_text;
    EventProxyPtr m_eventProxy;
};

#endif
