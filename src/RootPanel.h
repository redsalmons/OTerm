#ifndef ROOT_PANEL_H
#define ROOT_PANEL_H

#include <wx/wx.h>
#include "TerminalPanel.h"
#include "InfiniteSplitter.h"

class RootPanel : public wxPanel {
public:
    RootPanel(wxWindow* parent);
    ~RootPanel();

    // 封装接口，确保顶层第一层也能无缝调用 Replace 逻辑
    void ReplaceChildWithSplitter(wxWindow* childToReplace, wxSplitMode mode);

private:
    InfiniteSplitter* m_rootSplitter;
};

#endif
