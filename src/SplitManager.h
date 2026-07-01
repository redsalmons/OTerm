#pragma once

#include <wx/weakref.h>
#include "SplitTree.h"
#include "SplitNode.h"
#include "SplitContainer.h"
#include "TermGLCanvas.h"
#include <memory>

wxDECLARE_EVENT(wxEVT_CLOSE_PANEL, wxCommandEvent);

class SplitManager {
public:
    SplitManager(wxWindow* parent);
    ~SplitManager();
    
    void Initialize(std::shared_ptr<ISplitable> rootContent);
    void Split(ISplitable* target, wxSplitMode mode);
    void Close(TerminalPanel* panel);
    void ApplySplitCallbackToPanel(TerminalPanel* panel);
    
    wxWindow* GetRootWindow() const;
    void Freeze();
    void Thaw();
    
    SplitTree* GetTree() { return m_tree.get(); }
    
    void SetSplitCallback(TermGLCanvas::SplitCallback callback) { m_splitCallback = callback; }
    void SetCloseCallback(TermGLCanvas::CloseCallback callback) { m_closeCallback = callback; }
    
private:
    wxWeakRef<wxWindow> m_parent;
    std::unique_ptr<SplitTree> m_tree;
    TermGLCanvas::SplitCallback m_splitCallback;
    TermGLCanvas::CloseCallback m_closeCallback;
    std::shared_ptr<bool> m_alive; // 生命周期标志
    
    SplitContainer* CreateContainer(wxWindow* parent, wxSplitMode mode);
    void PerformSplit(SplitNode* targetNode, wxSplitMode mode, std::shared_ptr<ISplitable> newContent);
    void RebuildUIFromTree();
    void RebuildUIRecursive(SplitNode* node, wxWindow* parentWindow);
    void DoClose(TerminalPanel* panel);
    void OnClosePanelEvent(wxCommandEvent& event);
    
    int m_lastClosePageIndex = -1;
};
