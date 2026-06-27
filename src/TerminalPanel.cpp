#include "TerminalPanel.h"
#include "InfiniteSplitter.h"
#include "TerminalThread.h"
#include <wx/menu.h>
#include <wx/splitter.h>
#include <fstream>
#include <filesystem>

#define SPLIT_LOG(msg) \
    do { \
        std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app); \
        if (f.is_open()) f << "[TERMINAL] " << msg << std::endl; \
    } while(0)

TerminalPanel::TerminalPanel(wxWindow* parent, LocalTerminalContainer* container)
    : wxPanel(parent, wxID_ANY),
      m_terminalContainer(container),
      m_canvas(nullptr),
      m_text(nullptr),
      m_eventProxy(std::make_shared<EventProxy>()) {
    
    SPLIT_LOG("TerminalPanel constructor START, container=" << container);
    
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    SetSizer(sizer);
    
    // 设置最小尺寸，防止面板塌陷
    SetMinSize(wxSize(100, 100));

    // 绑定终端损坏事件
    Bind(wxEVT_TERMINAL_DAMAGE, &TerminalPanel::OnTerminalDamage, this);
    
    // Don't bind menu events - TermGLCanvas will handle split
    // Bind(wxEVT_MENU, &TerminalPanel::OnHorizontalSplit, this, ID_HORIZONTAL_SPLIT);
    // Bind(wxEVT_MENU, &TerminalPanel::OnVerticalSplit, this, ID_VERTICAL_SPLIT);
    // Bind(wxEVT_MENU, &TerminalPanel::OnClosePanel, this, ID_CLOSE_PANEL);

    if (m_terminalContainer) {
        // 设置 UI handler 为当前面板
        m_terminalContainer->SetUIHandler(this);
        // 让容器使用面板的EventProxy
        m_terminalContainer->GetThread()->SetEventProxy(m_eventProxy);
        SPLIT_LOG("TerminalPanel attached container and set EventProxy");
    }
    
    // Don't bind right-click - TermGLCanvas will handle it
    // Bind(wxEVT_RIGHT_DOWN, &TerminalPanel::OnMouseRightDown, this);
    
    SPLIT_LOG("TerminalPanel constructor DONE");
}

TerminalPanel::~TerminalPanel() {
    SPLIT_LOG("TerminalPanel destructor called");
    
    // Unbind events to prevent crash
    Unbind(wxEVT_TERMINAL_DAMAGE, &TerminalPanel::OnTerminalDamage, this);
    
    // Clear EventProxy target to prevent callbacks to destroyed windows
    if (m_eventProxy) {
        m_eventProxy->SetTarget(nullptr);
        m_eventProxy->SetDamageCallback(nullptr);
        m_eventProxy->SetInputCallback(nullptr);
    }
    
    // 清除 UI handler（但不删除容器，容器由调用者管理）
    if (m_terminalContainer) {
        // 先清除 UI handler，防止线程继续向已销毁的面板发送事件
        m_terminalContainer->ClearUIHandler();
    }
    // 不删除终端容器，由调用者管理
    m_terminalContainer = nullptr;
    // 不删除 canvas，由调用者管理
    m_canvas = nullptr;
    SPLIT_LOG("TerminalPanel destructor done");
}

void TerminalPanel::SetTerminalContainer(LocalTerminalContainer* container) {
    SPLIT_LOG("SetTerminalContainer called: " << container);
    
    // 清除旧的 UI handler
    if (m_terminalContainer) {
        m_terminalContainer->ClearUIHandler();
    }
    
    // 移除占位文本
    wxSizer* sizer = GetSizer();
    if (sizer && m_text) {
        sizer->Detach(m_text);
        delete m_text;
        m_text = nullptr;
    }
    
    m_terminalContainer = container;
    
    if (m_terminalContainer) {
        // 设置 UI handler 为当前面板
        m_terminalContainer->SetUIHandler(this);
        SPLIT_LOG("TerminalPanel attached container");
        
        // 如果canvas存在，建立连接
        if (m_canvas) {
            SetupCanvasConnection();
        }
    } else {
        // 显示占位文本
        m_text = new wxStaticText(this, wxID_ANY, wxT("Terminal Panel"));
        sizer->Add(m_text, 1, wxALIGN_CENTER | wxALL, 10);
    }
    
    sizer->Layout();
}

void TerminalPanel::SetCanvas(TermGLCanvas* canvas) {
    SPLIT_LOG("SetCanvas called: " << canvas << " parent=" << (canvas ? canvas->GetParent() : 0) << " this=" << this);
    
    // 移除旧的canvas
    wxSizer* sizer = GetSizer();
    if (sizer && m_canvas) {
        sizer->Detach(m_canvas);
        SPLIT_LOG("Detached old canvas");
    }
    
    m_canvas = canvas;
    
    if (m_canvas) {
        // 不再需要 reparent，canvas 父窗口保持不变
        // 添加新的canvas到sizer
        sizer->Add(m_canvas, 1, wxEXPAND);
        m_canvas->Show(true);
        m_canvas->SetMinSize(wxSize(100, 100));
        SPLIT_LOG("Added canvas to sizer and showed it");
        
        // 重新初始化 OpenGL 上下文
        m_canvas->ReinitializeGLContext();
        SPLIT_LOG("Reinitialized GL context");
        
        // 如果container存在，建立连接
        if (m_terminalContainer) {
            SetupCanvasConnection();
            SPLIT_LOG("Setup canvas connection");
        }
    }
    
    sizer->Layout();
    Refresh();
    Update();
    SPLIT_LOG("SetCanvas done");
}

void TerminalPanel::SetupCanvasConnection() {
    if (!m_canvas || !m_terminalContainer) {
        return;
    }
    
    // Set EventProxy target to canvas
    m_eventProxy->SetTarget(m_canvas);
    
    // Set damage callback to trigger canvas refresh
    m_eventProxy->SetDamageCallback([this](int rows, int cols, int cursor_row, int cursor_col, int first_nonempty_char) {
        if (m_canvas) {
            // Trigger OnTerminalDamage on the canvas
            wxThreadEvent evt(wxEVT_TERMINAL_DAMAGE);
            wxQueueEvent(m_canvas, evt.Clone());
        }
    });
    
    // 设置 canvas 的输入回调
    m_canvas->SetKeyCallback([this](const char* data, int length) {
        if (m_terminalContainer) {
            m_terminalContainer->QueueInput(std::string(data, length));
        }
    });
    
    // 初始渲染
    UpdateCanvasFromTerminal();
    
    SPLIT_LOG("Canvas connection setup done");
}

void TerminalPanel::DoSplit(wxSplitMode mode) {
    SPLIT_LOG("DoSplit called, mode=" << mode);
    SPLIT_LOG("this=" << this << " container=" << m_terminalContainer << " canvas=" << m_canvas);
    wxWindow* parent = GetParent();
    SPLIT_LOG("parent=" << parent);
    
    // 检查是否是 InfiniteSplitter
    InfiniteSplitter* splitter = wxDynamicCast(parent, InfiniteSplitter);
    if (splitter) {
        SPLIT_LOG("Parent is InfiniteSplitter, IsSplit=" << splitter->IsSplit());
        // 直接调用 InfiniteSplitter 的 ReplaceChildWithSplitter
        splitter->ReplaceChildWithSplitter(this, mode);
    } else {
        SPLIT_LOG("ERROR: Parent is not InfiniteSplitter");
    }
    SPLIT_LOG("DoSplit done");
}

void TerminalPanel::OnClosePanel(wxCommandEvent& event) {
    SPLIT_LOG("OnClosePanel called");
    wxWindow* parent = GetParent();
    SPLIT_LOG("parent=" << parent);
    
    // 检查是否是 InfiniteSplitter
    InfiniteSplitter* splitter = wxDynamicCast(parent, InfiniteSplitter);
    if (splitter) {
        SPLIT_LOG("Parent is InfiniteSplitter, IsSplit=" << splitter->IsSplit());
        // 如果 splitter 未 split，说明只剩一个面板，不允许关闭
        if (!splitter->IsSplit()) {
            SPLIT_LOG("Splitter not split, forwarding to grandparent");
            // 转发给父级处理
            wxWindow* grandParent = splitter->GetParent();
            if (grandParent) {
                InfiniteSplitter* parentSplitter = wxDynamicCast(grandParent, InfiniteSplitter);
                if (parentSplitter) {
                    // 父级是 InfiniteSplitter，尝试关闭这个 splitter
                    SPLIT_LOG("Grandparent is InfiniteSplitter, attempting to close this splitter");
                    if (parentSplitter->IsSplit()) {
                        parentSplitter->CloseChild(splitter);
                    } else {
                        // 爷爷级也未 split，继续向上查找
                        SPLIT_LOG("Grandparent also not split, continuing up");
                        wxWindow* greatGrandParent = parentSplitter->GetParent();
                        if (greatGrandParent) {
                            InfiniteSplitter* greatGrandParentSplitter = wxDynamicCast(greatGrandParent, InfiniteSplitter);
                            if (greatGrandParentSplitter && greatGrandParentSplitter->IsSplit()) {
                                greatGrandParentSplitter->CloseChild(parentSplitter);
                            } else {
                                SPLIT_LOG("Cannot find a split ancestor to close");
                            }
                        }
                    }
                } else {
                    SPLIT_LOG("Grandparent is not InfiniteSplitter");
                }
            } else {
                SPLIT_LOG("No grandparent");
            }
        } else {
            // splitter 已 split，正常关闭
            SPLIT_LOG("Splitter is split, closing this panel");
            splitter->CloseChild(this);
        }
    } else {
        SPLIT_LOG("Parent is not InfiniteSplitter");
        // 父级不是 InfiniteSplitter，检查是否是根 splitter（InfiniteSplitter 的父级）
        wxWindow* grandParent = parent->GetParent();
        SPLIT_LOG("grandParent=" << grandParent);
        if (grandParent) {
            InfiniteSplitter* grandParentSplitter = wxDynamicCast(grandParent, InfiniteSplitter);
            if (grandParentSplitter) {
                SPLIT_LOG("Grandparent is InfiniteSplitter, attempting to close parent");
                if (grandParentSplitter->IsSplit()) {
                    grandParentSplitter->CloseChild(parent);
                } else {
                    // 爷爷级也未 split，继续向上查找
                    SPLIT_LOG("Grandparent not split, continuing up");
                    wxWindow* greatGrandParent = grandParentSplitter->GetParent();
                    if (greatGrandParent) {
                        InfiniteSplitter* greatGrandParentSplitter = wxDynamicCast(greatGrandParent, InfiniteSplitter);
                        if (greatGrandParentSplitter && greatGrandParentSplitter->IsSplit()) {
                            greatGrandParentSplitter->CloseChild(grandParentSplitter);
                        } else {
                            SPLIT_LOG("Cannot find a split ancestor to close");
                        }
                    }
                }
            } else {
                SPLIT_LOG("Grandparent is not InfiniteSplitter");
            }
        } else {
            SPLIT_LOG("No grandparent");
        }
    }
    SPLIT_LOG("OnClosePanel done");
}

void TerminalPanel::OnTerminalDamage(wxThreadEvent& event) {
    SPLIT_LOG("OnTerminalDamage called");
    UpdateCanvasFromTerminal();
}

void TerminalPanel::UpdateCanvasFromTerminal() {
    if (!m_terminalContainer || !m_canvas) {
        return;
    }
    
    const ScreenBuffer* buffer = m_terminalContainer->GetFrontBuffer();
    if (!buffer) {
        return;
    }
    
    // 将 buffer 中的数据转换为 canvas 需要的格式
    std::vector<CellInstance> instances;
    for (int row = 0; row < buffer->rows; ++row) {
        for (int col = 0; col < buffer->cols; ++col) {
            const CellInstance& cell = buffer->cells[row][col];
            instances.push_back(cell);
        }
    }
    
    m_canvas->UpdateScreenData(instances);
    m_canvas->SetCursorPosition(buffer->cursor_row, buffer->cursor_col, 
                                  m_terminalContainer->GetThread()->IsInAlternateScreen(),
                                  m_terminalContainer->GetThread()->GetScrollOffset());
    m_canvas->Refresh();
}

void TerminalPanel::Activate() {
    SPLIT_LOG("TerminalPanel::Activate called");
    if (m_terminalContainer) {
        // Set this panel as the target for EventProxy
        m_terminalContainer->SetUIHandler(this);
        SPLIT_LOG("TerminalPanel activated, set UI handler");
    }
}

void TerminalPanel::Deactivate() {
    SPLIT_LOG("TerminalPanel::Deactivate called");
    if (m_terminalContainer) {
        // Clear the target from EventProxy
        m_terminalContainer->ClearUIHandler();
        SPLIT_LOG("TerminalPanel deactivated, cleared UI handler");
    }
}
