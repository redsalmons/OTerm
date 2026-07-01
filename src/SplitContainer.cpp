#include "SplitContainer.h"
#include "TerminalPanel.h"
#include "TermGLCanvas.h"
#include <functional>

SplitContainer::SplitContainer(wxWindow* parent, wxSplitMode mode)
    : wxPanel(parent, wxID_ANY), m_mode(mode) {
    SetBackgroundColour(wxColour(10, 10, 10));
    
    m_sash = new wxPanel(this, wxID_ANY);
    m_sash->SetBackgroundColour(wxColour(80, 80, 80));
    
    if (mode == wxSPLIT_VERTICAL) {
        m_sash->SetCursor(wxCURSOR_SIZEWE);
        m_sash->SetMinSize(wxSize(4, -1));
    } else {
        m_sash->SetCursor(wxCURSOR_SIZENS);
        m_sash->SetMinSize(wxSize(-1, 4));
    }
    
    m_sash->Bind(wxEVT_LEFT_DOWN, &SplitContainer::OnSashMouseLeftDown, this);
    m_sash->Bind(wxEVT_MOTION, &SplitContainer::OnSashMouseMotion, this);
    m_sash->Bind(wxEVT_LEFT_UP, &SplitContainer::OnSashMouseLeftUp, this);
    m_sash->Bind(wxEVT_ENTER_WINDOW, &SplitContainer::OnSashEnter, this);
    m_sash->Bind(wxEVT_LEAVE_WINDOW, &SplitContainer::OnSashLeave, this);
    
    Bind(wxEVT_SIZE, &SplitContainer::OnSize, this);
    Bind(wxEVT_LEFT_DOWN, &SplitContainer::OnMouseLeftDown, this);
}

SplitContainer::~SplitContainer() {
}

void SplitContainer::SetChildren(wxWindow* first, wxWindow* second) {
    m_first = first;
    m_second = second;
    
    if (m_first) {
        m_first->Reparent(this);
        m_first->Show(true);
    }
    if (m_second) {
        m_second->Reparent(this);
        m_second->Show(true);
    }
    
    UpdateLayout();
}

void SplitContainer::SetSashPosition(int pos) {
    m_sashPos = pos;
    
    // Update ratio based on current size
    wxSize size = GetClientSize();
    int total = (m_mode == wxSPLIT_VERTICAL) ? size.GetWidth() : size.GetHeight();
    if (total > 0) {
        m_sashRatio = static_cast<float>(m_sashPos) / total;
    }
    
    UpdateLayout();
}

void SplitContainer::ScaleSashPosition(float ratio) {
    m_sashRatio = ratio;
    wxSize size = GetClientSize();
    int total = (m_mode == wxSPLIT_VERTICAL) ? size.GetWidth() : size.GetHeight();
    if (total > 0) {
        m_sashPos = static_cast<int>(total * m_sashRatio);
    }
    UpdateLayout();
}

void SplitContainer::UpdateLayout() {
    if (!m_first || !m_second || !m_sash) return;
    
    wxSize size = GetClientSize();
    int totalWidth = size.GetWidth();
    int totalHeight = size.GetHeight();
    
    if (totalWidth <= 0 || totalHeight <= 0) return;
    
    int currentTotal = (m_mode == wxSPLIT_VERTICAL) ? totalWidth : totalHeight;
    
    // If size changed (including initial layout), update sashPos based on ratio
    // But ONLY if we are not currently dragging the sash
    if (currentTotal != m_lastTotalSize) {
        if (!m_dragging) {
            m_sashPos = static_cast<int>(currentTotal * m_sashRatio);
        }
        m_lastTotalSize = currentTotal;
    }
    
    if (m_mode == wxSPLIT_VERTICAL) {
        int sashWidth = 4;
        int firstWidth = m_sashPos;
        if (firstWidth < 60) firstWidth = 60;
        if (firstWidth > totalWidth - 60 - sashWidth) firstWidth = totalWidth - 60 - sashWidth;
        
        m_first->SetSize(0, 0, firstWidth, totalHeight);
        m_sash->SetSize(firstWidth, 0, sashWidth, totalHeight);
        m_second->SetSize(firstWidth + sashWidth, 0, totalWidth - firstWidth - sashWidth, totalHeight);
    } else {
        int sashHeight = 4;
        int firstHeight = m_sashPos;
        if (firstHeight < 60) firstHeight = 60;
        if (firstHeight > totalHeight - 60 - sashHeight) firstHeight = totalHeight - 60 - sashHeight;
        
        m_first->SetSize(0, 0, totalWidth, firstHeight);
        m_sash->SetSize(0, firstHeight, totalWidth, sashHeight);
        m_second->SetSize(0, firstHeight + sashHeight, totalWidth, totalHeight - firstHeight - sashHeight);
    }
    
    m_first->Refresh();
    m_second->Refresh();
    m_sash->Refresh();
    
    // Recursively update children
    SplitContainer* firstSplit = dynamic_cast<SplitContainer*>(m_first);
    if (firstSplit) {
        firstSplit->UpdateLayout();
    } else {
        m_first->Layout();
    }
    
    SplitContainer* secondSplit = dynamic_cast<SplitContainer*>(m_second);
    if (secondSplit) {
        secondSplit->UpdateLayout();
    } else {
        m_second->Layout();
    }
}

void SplitContainer::OnSize(wxSizeEvent& event) {
    UpdateLayout();
    event.Skip();
}

void SplitContainer::OnSashMouseLeftDown(wxMouseEvent& event) {
    m_dragging = true;
    m_dragStart = wxGetMousePosition();
    m_dragStartSashPos = m_sashPos;
    m_sash->CaptureMouse();
}

void SplitContainer::OnSashMouseMotion(wxMouseEvent& event) {
    if (!m_dragging) return;
    
    wxPoint currentPos = wxGetMousePosition();
    wxPoint delta = currentPos - m_dragStart;
    
    int newSashPos;
    if (m_mode == wxSPLIT_VERTICAL) {
        newSashPos = m_dragStartSashPos + delta.x;
    } else {
        newSashPos = m_dragStartSashPos + delta.y;
    }
    
    // Clamp sash position
    wxSize size = GetClientSize();
    int total = (m_mode == wxSPLIT_VERTICAL) ? size.GetWidth() : size.GetHeight();
    if (total > 0) {
        if (newSashPos < 60) newSashPos = 60;
        if (newSashPos > total - 64) newSashPos = total - 64;
        
        m_sashPos = newSashPos;
        m_sashRatio = static_cast<float>(m_sashPos) / total;
    }
    
    UpdateLayout();
}

void SplitContainer::OnSashMouseLeftUp(wxMouseEvent& event) {
    if (m_dragging) {
        m_dragging = false;
        m_sash->ReleaseMouse();
    }
}

void SplitContainer::OnSashEnter(wxMouseEvent& event) {
    if (m_mode == wxSPLIT_VERTICAL) {
        m_sash->SetCursor(wxCURSOR_SIZEWE);
    } else {
        m_sash->SetCursor(wxCURSOR_SIZENS);
    }
}

void SplitContainer::OnSashLeave(wxMouseEvent& event) {
    if (!m_dragging) {
        m_sash->SetCursor(wxCURSOR_ARROW);
    }
}

void SplitContainer::OnMouseLeftDown(wxMouseEvent& event) {
    wxPoint pos = event.GetPosition();
    
    // Determine which child was clicked based on position
    wxSize size = GetClientSize();
    int sashPos = m_sashPos;
    
    bool clickedFirst = false;
    if (m_mode == wxSPLIT_VERTICAL) {
        clickedFirst = (pos.x < sashPos);
    } else {
        clickedFirst = (pos.y < sashPos);
    }
    
    // Find the TermGLCanvas in the clicked panel and set focus
    wxWindow* targetPanel = clickedFirst ? m_first : m_second;
    if (targetPanel) {
        // Check if it's a TerminalPanel
        TerminalPanel* panel = dynamic_cast<TerminalPanel*>(targetPanel);
        if (panel) {
            TermGLCanvas* canvas = panel->GetCanvas();
            if (canvas) {
                canvas->SetFocus();
                return;
            }
        }
        
        // Check if it's a SplitContainer (nested split)
        SplitContainer* container = dynamic_cast<SplitContainer*>(targetPanel);
        if (container) {
            // Recursively find the first TerminalPanel
            std::function<TermGLCanvas*(wxWindow*)> findCanvas = [&](wxWindow* win) -> TermGLCanvas* {
                if (!win) return nullptr;
                TerminalPanel* p = dynamic_cast<TerminalPanel*>(win);
                if (p) return p->GetCanvas();
                SplitContainer* c = dynamic_cast<SplitContainer*>(win);
                if (c) {
                    TermGLCanvas* canvas = findCanvas(c->GetFirst());
                    if (canvas) return canvas;
                    return findCanvas(c->GetSecond());
                }
                return nullptr;
            };
            TermGLCanvas* canvas = findCanvas(container);
            if (canvas) {
                canvas->SetFocus();
                return;
            }
        }
    }
    
    event.Skip();
}
