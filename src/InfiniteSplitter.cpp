#include "InfiniteSplitter.h"
#include "TerminalPanel.h"
#include "AppWindow.h"
#include <fstream>
#include <filesystem>

#define SPLIT_LOG(msg) \
    do { \
        std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app); \
        if (f.is_open()) f << "[SPLIT] " << msg << std::endl; \
    } while(0)

InfiniteSplitter::InfiniteSplitter(wxWindow* parent)
    : wxSplitterWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D) 
{
    // 防呆：防止拆分太小导致无法显示
    SetMinimumPaneSize(60);
    // 重力系数：当父窗体缩放时，子树完全 50/50 等比例跟随缩放，避免排版塌陷
    SetSashGravity(0.5);
    
    // Bind sash events to handle layout updates
    Bind(wxEVT_SPLITTER_SASH_POS_CHANGING, &InfiniteSplitter::OnSashPosChanging, this);
    Bind(wxEVT_SPLITTER_SASH_POS_CHANGED, &InfiniteSplitter::OnSashPosChanged, this);
    
    // Don't bind right-click event for now to avoid crash
    // Bind(wxEVT_RIGHT_DOWN, &InfiniteSplitter::OnMouseRightDown, this);
    
    // Don't bind menu events for now
    // Bind(wxEVT_MENU, &InfiniteSplitter::OnHorizontalSplit, this, wxID_HIGHEST + 300);
    // Bind(wxEVT_MENU, &InfiniteSplitter::OnVerticalSplit, this, wxID_HIGHEST + 301);
}

InfiniteSplitter::~InfiniteSplitter() {
    SPLIT_LOG("InfiniteSplitter destructor called");
    // Don't call Unsplit() as it causes crash
    SPLIT_LOG("InfiniteSplitter destructor done");
}

void InfiniteSplitter::Initialize(wxWindow* window) {
    SPLIT_LOG("Initialize called with window: " << window);
    if (window) {
        // Reparent window to splitter
        if (window->GetParent() != this) {
            window->Reparent(this);
            SPLIT_LOG("Reparented window to splitter");
        }
        
        window->Show(true);
        
        // 如果是TerminalPanel，激活它以设置EventProxy目标
        TerminalPanel* panel = wxDynamicCast(window, TerminalPanel);
        if (panel) {
            panel->Activate();
            TermGLCanvas* canvas = panel->GetCanvas();
            if (canvas) {
                canvas->Show(true);
                SPLIT_LOG("Showed canvas in Initialize");
            }
        }
        
        // Use base class Initialize to set the single window
        wxSplitterWindow::Initialize(window);
        
        // 刷新布局
        Layout();
        Refresh();
        Update();
    }
    SPLIT_LOG("Initialize done");
}

void InfiniteSplitter::ReplaceChildWithSplitter(wxWindow* childToReplace, wxSplitMode mode) {
    SPLIT_LOG("ReplaceChildWithSplitter called, IsSplit=" << IsSplit() << " mode=" << mode);
    SPLIT_LOG("childToReplace=" << childToReplace);
    
    // 获取原面板的终端容器和canvas
    TerminalPanel* originalPanel = wxDynamicCast(childToReplace, TerminalPanel);
    LocalTerminalContainer* originalContainer = nullptr;
    TermGLCanvas* originalCanvas = nullptr;
    if (originalPanel) {
        originalContainer = originalPanel->GetTerminalContainer();
        originalCanvas = originalPanel->GetCanvas();
        SPLIT_LOG("Original panel container: " << originalContainer << " canvas: " << originalCanvas);
    } else {
        SPLIT_LOG("ERROR: childToReplace is not a TerminalPanel");
        return;
    }
    
    // 如果当前 Splitter 还没有 split（只有一个窗口）
    if (!IsSplit()) {
        SPLIT_LOG("Not split, handling initial split");
        
        // 1. 记录旧UI的状态
        EventProxyPtr proxy = originalContainer ? originalContainer->GetEventProxy() : nullptr;
        SPLIT_LOG("Proxy: " << proxy.get());
        
        // 2. 清除原面板的container引用，避免共享
        if (originalPanel) {
            originalPanel->SetTerminalContainer(nullptr);
            SPLIT_LOG("Cleared original panel container reference");
        }
        
        // 3. 在新的Splitter下创建新的Panel/Canvas
        SPLIT_LOG("Creating new panel B with LocalTerminalContainer");
        TerminalPanel* newPanelB = new TerminalPanel(this, new LocalTerminalContainer(24, 80, ""));
        SPLIT_LOG("Created newPanelB: " << newPanelB);
        
        // 创建新的TermGLCanvas用于新panel
        SPLIT_LOG("Creating new TermGLCanvas for panel B");
        TermGLCanvas* newCanvasB = new TermGLCanvas(newPanelB, false);
        SPLIT_LOG("Created newCanvasB: " << newCanvasB);
        
        // 新panel设置canvas
        newPanelB->SetCanvas(newCanvasB);
        SPLIT_LOG("Set new canvas to panel B");
        
        // 4. 直接split当前splitter，使用原面板和新面板
        SPLIT_LOG("Splitting current splitter with original panel and new panel");
        bool splitResult = false;
        if (mode == wxSPLIT_VERTICAL) {
            splitResult = SplitVertically(originalPanel, newPanelB, 200);
        } else {
            splitResult = SplitHorizontally(originalPanel, newPanelB, 200);
        }
        SPLIT_LOG("Split result: " << splitResult);
        
        // 显示新面板
        newPanelB->Show(true);
        
        SPLIT_LOG("Layout refresh");
        Layout();
        Refresh();
        Update();
        
        SPLIT_LOG("ReplaceChildWithSplitter done");
        return;
    }
    
    SPLIT_LOG("Already split, nested split not supported for now");
    return;
}

void InfiniteSplitter::CloseChild(wxWindow* childToClose) {
    SPLIT_LOG("CloseChild called, IsSplit=" << IsSplit());
    
    if (!IsSplit()) {
        SPLIT_LOG("ERROR: CloseChild called on unsplit splitter");
        return;
    }

    wxWindow* w1 = GetWindow1();
    wxWindow* w2 = GetWindow2();
    
    SPLIT_LOG("w1=" << w1 << " w2=" << w2 << " childToClose=" << childToClose);
    
    // 找出得以保留的兄弟窗口
    wxWindow* remainingWindow = (childToClose == w1) ? w2 : w1;
    
    SPLIT_LOG("remainingWindow=" << remainingWindow);

    // 撤销对该关闭面板的跟踪
    Unsplit(childToClose);

    // 刷新全屏布局
    wxWindow* topLevel = this;
    while (topLevel && topLevel->GetParent()) {
        topLevel = topLevel->GetParent();
    }
    if (topLevel) {
        topLevel->Layout();
    }
    
    SPLIT_LOG("CloseChild done");
}

void InfiniteSplitter::OnMouseRightDown(wxMouseEvent& event) {
    SPLIT_LOG("InfiniteSplitter::OnMouseRightDown called");
    
    // Show context menu with split options
    wxMenu menu;
    menu.Append(wxID_HIGHEST + 300, "Horizontal Split");
    menu.Append(wxID_HIGHEST + 301, "Vertical Split");
    
    SPLIT_LOG("Showing context menu at position: " << event.GetPosition().x << "," << event.GetPosition().y);
    PopupMenu(&menu, event.GetPosition());
    SPLIT_LOG("PopupMenu returned");
}

void InfiniteSplitter::OnHorizontalSplit(wxCommandEvent& event) {
    SPLIT_LOG("InfiniteSplitter::OnHorizontalSplit called");
    wxWindow* w1 = GetWindow1();
    if (w1) {
        TerminalPanel* panel = wxDynamicCast(w1, TerminalPanel);
        if (panel) {
            SPLIT_LOG("Calling DoSplit on panel");
            panel->DoSplit(wxSPLIT_HORIZONTAL);
        }
    }
}

void InfiniteSplitter::OnVerticalSplit(wxCommandEvent& event) {
    SPLIT_LOG("InfiniteSplitter::OnVerticalSplit called");
    wxWindow* w1 = GetWindow1();
    if (w1) {
        TerminalPanel* panel = wxDynamicCast(w1, TerminalPanel);
        if (panel) {
            SPLIT_LOG("Calling DoSplit on panel");
            panel->DoSplit(wxSPLIT_VERTICAL);
        }
    }
}

void InfiniteSplitter::OnSashPosChanging(wxSplitterEvent& event) {
    SPLIT_LOG("OnSashPosChanging called");
    event.Skip();
}

void InfiniteSplitter::OnSashPosChanged(wxSplitterEvent& event) {
    SPLIT_LOG("OnSashPosChanged called");
    // Force layout update
    Layout();
    Refresh();
    event.Skip();
}
