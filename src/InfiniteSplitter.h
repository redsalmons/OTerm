#ifndef INFINITE_SPLITTER_H
#define INFINITE_SPLITTER_H

#include <wx/wx.h>
#include <wx/splitter.h>

class TerminalPanel;
class AppWindow;

// ===========================================================================
// 无限拆分容器（分支节点）
// ===========================================================================
class InfiniteSplitter : public wxSplitterWindow {
public:
    InfiniteSplitter(wxWindow* parent);
    virtual ~InfiniteSplitter();

    // 核心算法：用一个新的 Splitter 容器替换掉当前指定的旧面板，并分裂为两个新面板
    void ReplaceChildWithSplitter(wxWindow* childToReplace, wxSplitMode mode);
    
    // 关闭当前面板，让剩下的兄弟面板独占该容器
    void CloseChild(wxWindow* childToClose);
    
    // 初始化 splitter（设置初始窗口）
    void Initialize(wxWindow* window);
    
    // 设置主窗口指针，用于统一销毁管理
    void SetMainWindow(AppWindow* mainWindow) { m_mainWindow = mainWindow; }

private:
    void OnMouseRightDown(wxMouseEvent& event);
    void OnHorizontalSplit(wxCommandEvent& event);
    void OnVerticalSplit(wxCommandEvent& event);
    void OnSashPosChanging(wxSplitterEvent& event);
    void OnSashPosChanged(wxSplitterEvent& event);
    
    AppWindow* m_mainWindow = nullptr;
};

#endif
