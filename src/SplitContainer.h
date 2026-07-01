#pragma once

#include <wx/wx.h>
#include <wx/splitter.h>

// SplitContainer: a simple wxPanel that holds two child windows with a draggable sash.
// Replaces wxSplitterWindow - no wxSplitterWindow dependency.
class SplitContainer : public wxPanel {
public:
    SplitContainer(wxWindow* parent, wxSplitMode mode);
    ~SplitContainer();

    void SetChildren(wxWindow* first, wxWindow* second);
    
    wxWindow* GetFirst() const { return m_first; }
    wxWindow* GetSecond() const { return m_second; }
    wxSplitMode GetMode() const { return m_mode; }
    
    int GetSashPosition() const { return m_sashPos; }
    void SetSashPosition(int pos);
    
    // Scale sash position proportionally when container size changes
    void ScaleSashPosition(float ratio);
    void UpdateLayout();

private:
    wxWindow* m_first = nullptr;
    wxWindow* m_second = nullptr;
    wxSplitMode m_mode;
    wxPanel* m_sash = nullptr;
    int m_sashPos = 200;
    float m_sashRatio = 0.5f; // Store proportion
    bool m_dragging = false;
    wxPoint m_dragStart;
    int m_dragStartSashPos;
    int m_lastTotalSize = 0; // Track last size to detect changes
    
    void OnSashMouseLeftDown(wxMouseEvent& event);
    void OnSashMouseMotion(wxMouseEvent& event);
    void OnSashMouseLeftUp(wxMouseEvent& event);
    void OnSashEnter(wxMouseEvent& event);
    void OnSashLeave(wxMouseEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouseLeftDown(wxMouseEvent& event);
};
