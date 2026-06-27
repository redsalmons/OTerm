#include "RootPanel.h"
#include "TerminalPanel.h"
#include "InfiniteSplitter.h"
#include <fstream>
#include <filesystem>

#define ROOT_LOG(msg) \
    do { \
        std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app); \
        if (f.is_open()) f << "[ROOT] " << msg << std::endl; \
    } while(0)

RootPanel::RootPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY),
      m_rootSplitter(nullptr) {
    
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    SetSizer(sizer);

    // 创建根容器
    m_rootSplitter = new InfiniteSplitter(this);
    sizer->Add(m_rootSplitter, 1, wxEXPAND);

    // 放入全屏的第一个种子面板
    ROOT_LOG("Creating initial TerminalPanel");
    TerminalPanel* initPanel = new TerminalPanel(m_rootSplitter, 24, 80, "");
    ROOT_LOG("Initializing root splitter");
    m_rootSplitter->Initialize(initPanel);
    ROOT_LOG("RootPanel construction done");
}

RootPanel::~RootPanel() {
}

// 封装接口，确保顶层第一层也能无缝调用 Replace 逻辑
void RootPanel::ReplaceChildWithSplitter(wxWindow* childToReplace, wxSplitMode mode) {
    ROOT_LOG("ReplaceChildWithSplitter called, IsSplit=" << m_rootSplitter->IsSplit());
    
    if (!m_rootSplitter->IsSplit()) {
        ROOT_LOG("Root splitter not split, handling initial split");
        // 根 splitter 还没有 split，需要特殊处理
        // 创建新的嵌套 splitter
        InfiniteSplitter* newNestedSplitter = new InfiniteSplitter(m_rootSplitter);
        
        // 创建两个新面板
        TerminalPanel* newPanelA = new TerminalPanel(newNestedSplitter, 24, 80, "");
        TerminalPanel* newPanelB = new TerminalPanel(newNestedSplitter, 24, 80, "");
        
        // 分割新的嵌套 splitter
        if (mode == wxSPLIT_VERTICAL) {
            newNestedSplitter->SplitVertically(newPanelA, newPanelB, 0);
        } else {
            newNestedSplitter->SplitHorizontally(newPanelA, newPanelB, 0);
        }
        
        ROOT_LOG("Destroying old panel");
        // 销毁旧面板
        childToReplace->Destroy();
        
        ROOT_LOG("Initializing with new nested splitter");
        // 用新的嵌套 splitter 初始化
        m_rootSplitter->Initialize(newNestedSplitter);
    } else {
        ROOT_LOG("Root splitter already split, delegating to splitter");
        // 已经 split，委托给 splitter 处理
        m_rootSplitter->ReplaceChildWithSplitter(childToReplace, mode);
    }
    
    ROOT_LOG("Layout refresh");
    Layout();
    ROOT_LOG("ReplaceChildWithSplitter done");
}
