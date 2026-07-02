#include "TerminalPanel.h"
#include "InfiniteSplitter.h"
#include "TerminalThread.h"
#include "GlobalConfig.h"
#include "AppWindow.h"
#include <wx/menu.h>
#include <wx/splitter.h>
#include <fstream>
#include <filesystem>

#define SPLIT_LOG(msg) ((void)0)

TerminalPanel::TerminalPanel(wxWindow* parent, std::unique_ptr<LocalTerminalContainer> container)
    : wxPanel(parent, wxID_ANY),
      m_terminalContainer(std::move(container)),
      m_canvas(nullptr),
      m_text(nullptr),
      m_eventProxy(std::make_shared<EventProxy>()) {
    
    SPLIT_LOG("TerminalPanel constructor START, container=" << m_terminalContainer.get());
    SPLIT_LOG("TerminalPanel constructor, m_terminalContainer=" << m_terminalContainer.get() << ", IsLocalTerminal=" << IsLocalTerminal());
    
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    SetSizer(sizer);
    
    // Set background color to match terminal to avoid white flashes
    SetBackgroundColour(wxColour(10, 10, 10));
    
    // 设置最小尺寸，防止面板塌陷
    SetMinSize(wxSize(100, 100));

    // 绑定终端损坏事件
    Bind(wxEVT_TERMINAL_DAMAGE, &TerminalPanel::OnTerminalDamage, this);
    Bind(wxEVT_SIZE, &TerminalPanel::OnSize, this);
    Bind(wxEVT_KEY_DOWN, &TerminalPanel::OnKeyDown, this);
    Bind(wxEVT_COMMAND_MENU_SELECTED, &TerminalPanel::OnFileTransferRequest, this);
    
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
    
    // 清除 UI handler
    if (m_terminalContainer) {
        // 先清除 UI handler，防止线程继续向已销毁的面板发送事件
        m_terminalContainer->ClearUIHandler();
    }
    
    // std::unique_ptr will automatically delete m_terminalContainer
    // m_terminalContainer = nullptr;
    
    // 不删除 canvas，由 wxWidgets 自动管理其生命周期（作为子窗口）
    m_canvas = nullptr;
    SPLIT_LOG("TerminalPanel destructor done");
}

void TerminalPanel::Shutdown() {
    SPLIT_LOG("TerminalPanel::Shutdown called");
    
    // Unbind events immediately to stop processing any new messages
    Unbind(wxEVT_TERMINAL_DAMAGE, &TerminalPanel::OnTerminalDamage, this);
    Unbind(wxEVT_SIZE, &TerminalPanel::OnSize, this);
    
    // Stop local terminal thread
    if (m_terminalContainer) {
        m_terminalContainer->StopTerminal();
        m_terminalContainer->ClearUIHandler();
    }
    
    // Clear SSH thread reference (it's owned by ConnectInfo)
    m_sshThread = nullptr;
    
    // Clear EventProxy
    if (m_eventProxy) {
        m_eventProxy->SetTarget(nullptr);
        m_eventProxy->SetDamageCallback(nullptr);
        m_eventProxy->SetInputCallback(nullptr);
    }
    
    SPLIT_LOG("TerminalPanel::Shutdown done");
}

void TerminalPanel::SetTerminalContainer(std::unique_ptr<LocalTerminalContainer> container) {
    SPLIT_LOG("SetTerminalContainer called: " << container.get());
    
    // 清除旧的 UI handler 和容器
    if (m_terminalContainer) {
        m_terminalContainer->ClearUIHandler();
    }
    
    m_terminalContainer = std::move(container);
    
    // 设置新的 UI handler
    if (m_terminalContainer) {
        m_terminalContainer->SetUIHandler(this);
        m_terminalContainer->GetThread()->SetEventProxy(m_eventProxy);
        SPLIT_LOG("SetTerminalContainer: attached new container and set EventProxy");
    }
    
    // 移除占位文本
    wxSizer* sizer = GetSizer();
    if (sizer && m_text) {
        sizer->Detach(m_text);
        delete m_text;
        m_text = nullptr;
    }
    
    m_prevRows = 0;
    m_prevCols = 0;
    
    if (m_terminalContainer) {
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
    
    // 手动触发一次 Resize 计算
    wxSizeEvent sizeEvt(GetSize(), GetId());
    OnSize(sizeEvt);
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
    
    // 手动触发一次 Resize 计算
    wxSizeEvent sizeEvt(GetSize(), GetId());
    OnSize(sizeEvt);
    
    Refresh();
    Update();
    SPLIT_LOG("SetCanvas done");
}

void TerminalPanel::SetupCanvasConnection() {
    SPLIT_LOG("SetupCanvasConnection START, canvas=" << m_canvas);
    
    if (!m_canvas) {
        SPLIT_LOG("SetupCanvasConnection FAILED: canvas is null");
        return;
    }
    
    // Set EventProxy target to canvas
    m_eventProxy->SetTarget(m_canvas);
    SPLIT_LOG("EventProxy target set to canvas");
    
    // Ensure canvas has reference to the thread
    if (m_sshThread) {
        m_canvas->m_terminalThread = m_sshThread;
        SPLIT_LOG("Set SSH terminal thread pointer on canvas");
    } else if (m_terminalContainer) {
        m_canvas->m_localTerminalThread = m_terminalContainer->GetThread();
        SPLIT_LOG("Set local terminal thread pointer on canvas");
    }
    
    // Set damage callback to trigger canvas refresh
    m_eventProxy->SetDamageCallback([this](int rows, int cols, int cursor_row, int cursor_col, int first_nonempty_char) {
        // Trigger OnTerminalDamage on this panel
        wxThreadEvent evt(wxEVT_TERMINAL_DAMAGE);
        wxQueueEvent(this, evt.Clone());
    });
    SPLIT_LOG("Damage callback set");
    
    // 设置 canvas 的 input 回调
    m_canvas->SetKeyCallback([this](const char* data, int length) {
        SPLIT_LOG("Key callback invoked: length=" << length << " first=" << (length > 0 ? (int)(unsigned char)data[0] : 0));
        if (m_sshThread) {
            m_sshThread->QueueInput(std::string(data, length));
        } else if (m_terminalContainer) {
            m_terminalContainer->QueueInput(std::string(data, length));
        }
    });
    SPLIT_LOG("Key callback set");

    // 设置 canvas 的 scroll 回调
    m_canvas->SetScrollCallback([this](int lines) {
        SPLIT_LOG("Scroll callback invoked: lines=" << lines);
        if (m_sshThread) {
            m_sshThread->ScrollVTerm(lines);
        } else if (m_terminalContainer) {
            LocalTerminalThread* thread = m_terminalContainer->GetThread();
            if (thread) {
                thread->ScrollVTerm(lines);
            }
        }
    });
    SPLIT_LOG("Scroll callback set");

    // 设置 canvas 的 mouse 回调
    m_canvas->SetMouseCallback([this](int row, int col, int button) {
        bool inAltScreen = m_sshThread ? m_sshThread->IsInAlternateScreen() : (m_terminalContainer && m_terminalContainer->GetThread() ? m_terminalContainer->GetThread()->IsInAlternateScreen() : false);
        SPLIT_LOG("Mouse callback invoked: row=" << row << " col=" << col << " button=" << button << " inAltScreen=" << inAltScreen);
        if (inAltScreen) {
            char seq[6];
            seq[0] = '\x1b';
            seq[1] = '[';
            seq[2] = 'M';
            seq[3] = (char)(32 + button);
            seq[4] = (char)(33 + col);
            seq[5] = (char)(33 + row);
            std::string seq_str(seq, 6);
            if (m_sshThread) {
                m_sshThread->QueueInput(seq_str);
            } else if (m_terminalContainer) {
                m_terminalContainer->QueueInput(seq_str);
            }
        }
    });
    SPLIT_LOG("Mouse callback set");
    
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

void TerminalPanel::OnFileTransferRequest(wxCommandEvent& event) {
    SPLIT_LOG("TerminalPanel::OnFileTransferRequest called");
    if (event.GetInt() == 2) {
        wxString command = event.GetString();
        SPLIT_LOG("File transfer request for command: " << command.ToStdString());
        
        // Forward the event to parent to find ConnectInfo
        wxWindow* parent = GetParent();
        while (parent) {
            wxQueueEvent(parent, event.Clone());
            parent = parent->GetParent();
        }
    }
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

void TerminalPanel::OnSize(wxSizeEvent& event) {
    Layout();
    if ((m_terminalContainer || m_sshThread) && m_canvas) {
        wxSize size = GetClientSize();
        
        // Get font height for minimum height check
        int cellHeight = m_canvas->m_cellHeight;
        if (cellHeight <= 0) {
            float dpiScale = m_canvas->m_dpiScale;
            cellHeight = GlobalConfig::GetTerminalFontSize(dpiScale);
            if (cellHeight < 12) cellHeight = 12;
        }
        
        // Skip resize if height is less than 1.5x font height (not enough space for 1.5 lines)
        if (size.GetHeight() < static_cast<int>(cellHeight * 1.5)) {
            SPLIT_LOG("TerminalPanel::OnSize [" << this << "] - Panel height too small, skipping resize: " << size.GetWidth() << "x" << size.GetHeight() << " (min height=" << static_cast<int>(cellHeight * 1.5) << ")");
            event.Skip();
            return;
        }
        
        // Also check minimum width (at least 10 characters)
        int cellWidth = m_canvas->m_cellWidth;
        if (cellWidth <= 0) {
            cellWidth = cellHeight / 2;
            if (cellWidth < 6) cellWidth = 6;
        }
        if (size.GetWidth() < cellWidth * 10) {
            SPLIT_LOG("TerminalPanel::OnSize [" << this << "] - Panel width too small, skipping resize: " << size.GetWidth() << "x" << size.GetHeight() << " (min width=" << (cellWidth * 10) << ")");
            event.Skip();
            return;
        }
        
        float dpiScale = m_canvas->m_dpiScale;
        
        // Robust fallback: if canvas hasn't initialized font metrics yet, calculate from config
        if (cellWidth <= 0 || cellHeight <= 0) {
            int terminalFontSize = GlobalConfig::GetTerminalFontSize(dpiScale);
            
            cellWidth = terminalFontSize / 2;
            cellHeight = terminalFontSize;
            
            if (cellWidth < 6) cellWidth = 6;
            if (cellHeight < 12) cellHeight = 12;
            
            SPLIT_LOG("TerminalPanel::OnSize [" << this << "] - Using fallback metrics: " << cellWidth << "x" << cellHeight);
        } else {
            SPLIT_LOG("TerminalPanel::OnSize [" << this << "] - Using canvas metrics: " << cellWidth << "x" << cellHeight);
        }
        
        int margin_x = static_cast<int>(8 * dpiScale);
        int margin_y = static_cast<int>(4 * dpiScale);

        int availableHeight = size.GetHeight() - margin_y * 2;
        int availableWidth = size.GetWidth() - margin_x * 2;

        if (availableHeight < 20) availableHeight = 20;
        if (availableWidth < 20) availableWidth = 20;

        int rows = availableHeight / cellHeight;
        int cols = availableWidth / cellWidth;

        if (rows < 2) rows = 2;
        if (cols < 10) cols = 10;

        if (rows != m_prevRows || cols != m_prevCols) {
            SPLIT_LOG("TerminalPanel::OnSize [" << this << "] - Resizing vterm to " << rows << "x" << cols 
                      << " (Panel size: " << size.GetWidth() << "x" << size.GetHeight() 
                      << ", Available: " << availableWidth << "x" << availableHeight 
                      << ", Prev: " << m_prevRows << "x" << m_prevCols << ")");
            
            if (m_sshThread) {
                m_sshThread->ResizeVTerm(rows, cols);
            } else if (m_terminalContainer) {
                m_terminalContainer->Resize(rows, cols);
            }
            
            m_prevRows = rows;
            m_prevCols = cols;
            
            // Force canvas update after resize
            UpdateCanvasFromTerminal();
        }
    }
    event.Skip();
}

void TerminalPanel::OnTerminalDamage(wxThreadEvent& event) {
    SPLIT_LOG("OnTerminalDamage called");
    // Check if panel is being destroyed to prevent use-after-free
    if (IsBeingDeleted()) {
        SPLIT_LOG("OnTerminalDamage: panel is being deleted, ignoring event");
        return;
    }
    UpdateCanvasFromTerminal();
}

void TerminalPanel::UpdateCanvasFromTerminal() {
    if (!m_canvas) {
        return;
    }
    
    ScreenBuffer local_buffer;
    bool in_alt_screen = false;
    int scroll_offset = 0;
    bool has_buffer = false;
    
    if (m_sshThread) {
        m_sshThread->CopyFrontBuffer(local_buffer);
        in_alt_screen = m_sshThread->IsInAlternateScreen();
        scroll_offset = m_sshThread->GetScrollOffset();
        has_buffer = true;
    } else if (m_terminalContainer) {
        LocalTerminalThread* thread = m_terminalContainer->GetThread();
        if (!thread) {
            // Thread has been stopped/deleted (e.g. during panel close). Nothing to render.
            return;
        }
        thread->CopyFrontBuffer(local_buffer);
        in_alt_screen = thread->IsInAlternateScreen();
        scroll_offset = thread->GetScrollOffset();
        has_buffer = true;
    }
    
    if (!has_buffer) {
        return;
    }
    
    // 将 buffer 中的数据转换为 canvas 需要的格式
    std::vector<CellInstance> instances;
    int rows = local_buffer.rows;
    int cols = local_buffer.cols;
    
    // Safety check: ensure buffer.cells has the expected size
    if ((int)local_buffer.cells.size() < rows) rows = (int)local_buffer.cells.size();
    
    for (int row = 0; row < rows; ++row) {
        int row_cols = cols;
        if ((int)local_buffer.cells[row].size() < row_cols) row_cols = (int)local_buffer.cells[row].size();
        
        for (int col = 0; col < row_cols; ++col) {
            CellInstance inst = local_buffer.cells[row][col];
            inst.cell_x = (float)col;
            inst.cell_y = (float)row;
            instances.push_back(inst);
        }
    }
    
    static int updateCount = 0;
    if (updateCount++ % 60 == 0) {
        SPLIT_LOG("UpdateCanvasFromTerminal: sending " << instances.size() << " cells (" << rows << "x" << cols << ") to canvas");
    }
    
    m_canvas->UpdateScreenData(instances);
    m_canvas->SetCursorPosition(local_buffer.cursor_row, local_buffer.cursor_col, in_alt_screen, scroll_offset);
    m_canvas->Refresh();
}

void TerminalPanel::StopThreads() {
    if (m_canvas) {
        m_canvas->StopThreads();
    }
}

void TerminalPanel::Activate() {
    SPLIT_LOG("TerminalPanel::Activate called");
    if (m_terminalContainer) {
        // Set this panel as the target for EventProxy
        m_terminalContainer->SetUIHandler(this);
        SPLIT_LOG("TerminalPanel activated, set UI handler for local");
    }
    
    if (m_eventProxy && m_canvas) {
        m_eventProxy->SetTarget(m_canvas);
        SPLIT_LOG("TerminalPanel activated, set EventProxy target to canvas");
    }
}

void TerminalPanel::Deactivate() {
    SPLIT_LOG("TerminalPanel::Deactivate called");
    if (m_terminalContainer) {
        // Clear the target from EventProxy
        m_terminalContainer->ClearUIHandler();
        SPLIT_LOG("TerminalPanel deactivated, cleared UI handler for local");
    }

    if (m_eventProxy) {
        m_eventProxy->SetTarget(nullptr);
        SPLIT_LOG("TerminalPanel deactivated, cleared EventProxy target");
    }
}

void TerminalPanel::AppendToInputBuffer(const std::string& text) {
    m_inputBuffer += text;
    SPLIT_LOG("TerminalPanel::AppendToInputBuffer: buffer now = '" << m_inputBuffer << "'");
}

void TerminalPanel::ClearInputBuffer() {
    SPLIT_LOG("TerminalPanel::ClearInputBuffer: clearing buffer (was '" << m_inputBuffer << "')");
    m_inputBuffer.clear();
}

void TerminalPanel::ConvertToSSH(const DeviceConfig& device) {
    SPLIT_LOG("TerminalPanel::ConvertToSSH called for device: " << device.name);
    
    // Stop and clear local terminal
    if (m_terminalContainer) {
        m_terminalContainer->StopTerminal();
        m_terminalContainer->ClearUIHandler();
        m_terminalContainer.reset();
    }
    
    // Use actual current rows and columns
    int initialRows = (m_prevRows > 0) ? m_prevRows : 24;
    int initialCols = (m_prevCols > 0) ? m_prevCols : 80;
    SPLIT_LOG("TerminalPanel::ConvertToSSH - Creating thread with actual size: " << initialRows << "x" << initialCols);
    
    // Create SSH thread
    m_sshThread = new TerminalThread(m_eventProxy, initialRows, initialCols, device);
    
    // Re-establish canvas connection with SSH thread before starting it
    SetupCanvasConnection();
    
    // Start the thread after connection is set up
    wxThreadError createErr = m_sshThread->Create();
    if (createErr != wxTHREAD_NO_ERROR) {
        SPLIT_LOG("TerminalPanel::ConvertToSSH - Create() failed, err=" << createErr);
        return;
    }
    
    wxThreadError runErr = m_sshThread->Run();
    if (runErr != wxTHREAD_NO_ERROR) {
        SPLIT_LOG("TerminalPanel::ConvertToSSH - Run() failed, err=" << runErr);
        return;
    }
    
    SPLIT_LOG("TerminalPanel::ConvertToSSH completed, thread started successfully");
}

void TerminalPanel::OnKeyDown(wxKeyEvent& event) {
    SPLIT_LOG("TerminalPanel::OnKeyDown: keycode=" << event.GetKeyCode());

    // Handle RETURN key for command interception
    if (event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER) {
        if (IsLocalTerminal()) {
            SPLIT_LOG("TerminalPanel::OnKeyDown: RETURN pressed on local terminal, checking command interception");
            SPLIT_LOG("TerminalPanel::OnKeyDown: inputBuffer='" << m_inputBuffer << "'");
            auto result = m_commandInterceptor.ShouldIntercept(m_inputBuffer);
            if (result == CommandInterceptor::InterceptionResult::Intercepted) {
                SPLIT_LOG("TerminalPanel::OnKeyDown: Command intercepted, checking command type");
                if (m_inputBuffer.find("oc device") == 0) {
                    SPLIT_LOG("TerminalPanel::OnKeyDown: oc device command detected, triggering device show request");
                    std::string cmd = m_inputBuffer;
                    ClearInputBuffer();
                    wxCommandEvent deviceEvent(wxEVT_DEVICE_SHOW_REQUEST);
                    deviceEvent.SetEventObject(this);
                    wxWindow* topWindow = wxTheApp->GetTopWindow();
                    if (topWindow) {
                        wxQueueEvent(topWindow, deviceEvent.Clone());
                    }
                    return;
                } else if (m_inputBuffer.find("oc ssh") == 0) {
                    SPLIT_LOG("TerminalPanel::OnKeyDown: oc ssh command detected, triggering direct SSH connect");
                    std::string cmd = m_inputBuffer;
                    ClearInputBuffer();
                    wxCommandEvent sshEvent(wxEVT_SSH_DIRECT_CONNECT);
                    sshEvent.SetString(wxString::FromUTF8(cmd.c_str()));
                    sshEvent.SetEventObject(this);
                    wxWindow* topWindow = wxTheApp->GetTopWindow();
                    if (topWindow) {
                        wxQueueEvent(topWindow, sshEvent.Clone());
                    }
                    return;
                } else {
                    SPLIT_LOG("TerminalPanel::OnKeyDown: Command intercepted, sending Ctrl+C instead of Enter");
                    if (m_terminalContainer && m_terminalContainer->GetThread()) {
                        m_terminalContainer->GetThread()->QueueInput("\x03");
                    }
                    ClearInputBuffer();
                    return;
                }
            }
        }
    }

    event.Skip();
}
