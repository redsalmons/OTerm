#include "ConnectInfo.h"

#include <wx/simplebook.h>

#include "TermGLCanvas.h"

#include "TerminalPanel.h"

#include "RootPanel.h"

#include "TerminalThread.h"

#include "LocalTerminalThread.h"

#include "SSHManager.h"

#include "FileTransferTask.h"

#include "TranslationHelper.h"

#include "GlobalConfig.h"

#include "SplitManager.h"

#include <wx/display.h>

#include <nlohmann/json.hpp>

#include <chrono>



wxDEFINE_EVENT(wxEVT_TAB_CLOSE, wxCommandEvent);

wxDEFINE_EVENT(wxEVT_TAB_SELECTED, wxCommandEvent);



ConnectInfo::ConnectInfo(wxWindow* parent, const wxString& label, wxWindow* contentPanel, const DeviceConfig& deviceConfig, bool showCloseButton, bool isLocalTerminal)

    : wxPanel(parent, wxID_ANY), m_contentPanel(contentPanel), m_deviceConfig(deviceConfig),

      m_isActive(false), m_isHovered(false), m_isLocalTerminal(isLocalTerminal), m_terminalThread(nullptr), m_localTerminalThread(nullptr), m_termCanvas(nullptr),

      m_fileTransferDialog(nullptr), m_fileTransferThread(nullptr), m_prevRows(0), m_prevCols(0), m_cachedWidth(0)

    , m_splitManager(std::make_unique<SplitManager>(contentPanel->GetParent())), m_labelEditor(nullptr) {

    // Calculate DPI scale

    double dpiScale = 1.0;

    if (GetHandle()) {

        dpiScale = GetDPIScaleFactor();

    } else {

        int screenNum = wxDisplay::GetFromWindow(this);

        if (screenNum != wxNOT_FOUND) {

            wxDisplay display(screenNum);

            int dpi = display.GetPPI().GetWidth();

            dpiScale = static_cast<double>(dpi) / 96.0;

        }

    }

    if (dpiScale <= 0.0) dpiScale = 1.0;



    // Set tab height to 28px

    int baseTabHeight = 28;

#ifdef __APPLE__

    int scaledTabHeight = baseTabHeight;

#else

    int scaledTabHeight = static_cast<int>(baseTabHeight * dpiScale);

#endif

    // Set minimum height only, let width be set dynamically

    SetMinSize(wxSize(-1, scaledTabHeight));

    

    m_label = new wxStaticText(this, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);

    m_label->SetForegroundColour(*wxWHITE);

#ifdef __APPLE__

    {

        wxFont labelFont = m_label->GetFont();

        int labelPointSize = labelFont.GetPointSize();

        if (labelPointSize > 0) {

            labelFont.SetPointSize(std::max(1, labelPointSize));

            m_label->SetFont(labelFont);

        }

    }

#endif

    m_label->Refresh();

    int baseCloseButtonSize = 20;

#ifdef __APPLE__

    int scaledCloseButtonSize = baseCloseButtonSize;

#else

    int scaledCloseButtonSize = static_cast<int>(baseCloseButtonSize * dpiScale);

#endif

    m_closeButton = new wxButton(this, wxID_ANY, "x", wxDefaultPosition, wxSize(scaledCloseButtonSize, scaledCloseButtonSize), wxBORDER_NONE);

    m_closeButton->Show(showCloseButton);



    wxBoxSizer* h_sizer = new wxBoxSizer(wxHORIZONTAL);

    int slant = 10;

    int padding = 12; // Adequate padding to stay away from slanted borders

    if (!showCloseButton) {

        // Homepage tab: left padding, stretch spacer, label, stretch spacer, right padding

        h_sizer->AddSpacer(slant + padding);

        h_sizer->AddStretchSpacer(1);

        h_sizer->Add(m_label, 0, wxALIGN_CENTER_VERTICAL);

        h_sizer->AddStretchSpacer(1);

        h_sizer->AddSpacer(slant + padding);

    } else {

        // Left padding, label, stretch spacer, close button, right padding

        h_sizer->AddSpacer(slant + padding);

        h_sizer->Add(m_label, 0, wxALIGN_CENTER_VERTICAL);

        h_sizer->AddStretchSpacer(1); // Spacer in between label and close button

        h_sizer->Add(m_closeButton, 0, wxALIGN_CENTER_VERTICAL);

        h_sizer->AddSpacer(slant + padding);

    }



    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);

    v_sizer->AddStretchSpacer(1);

    v_sizer->Add(h_sizer, 0, wxEXPAND);

    v_sizer->AddStretchSpacer(1);

    SetSizer(v_sizer);



    // Bind events for all tabs (including non-terminal tabs like Dashboard)

    SetBackgroundStyle(wxBG_STYLE_PAINT);

    Bind(wxEVT_PAINT, &ConnectInfo::OnPaint, this);

    Bind(wxEVT_ENTER_WINDOW, &ConnectInfo::OnEnter, this);

    Bind(wxEVT_LEAVE_WINDOW, &ConnectInfo::OnLeave, this);

    m_closeButton->Bind(wxEVT_BUTTON, &ConnectInfo::OnClose, this);

    Bind(wxEVT_LEFT_DOWN, &ConnectInfo::OnSelected, this);

    m_label->Bind(wxEVT_LEFT_DOWN, &ConnectInfo::OnSelected, this);

    m_label->Bind(wxEVT_LEFT_DCLICK, &ConnectInfo::OnLabelDoubleClick, this);

    Bind(wxEVT_SIZE, &ConnectInfo::OnSize, this);

    Bind(wxEVT_COMMAND_MENU_SELECTED, &ConnectInfo::OnFileTransferRequest, this);
    Bind(wxEVT_FILE_TRANSFER_REQUEST, &ConnectInfo::OnFileTransferRequest, this);

    Bind(wxEVT_FILE_TRANSFER_PROGRESS, &ConnectInfo::OnFileTransferProgress, this);

    Bind(wxEVT_FILE_TRANSFER_COMPLETE, &ConnectInfo::OnFileTransferComplete, this);



    // Find TermGLCanvas from content panel (which should be a TerminalPanel)

    m_termCanvas = nullptr;

    if (m_contentPanel) {

        // Initialize SplitManager with the TerminalPanel for internal split management

        TerminalPanel* panel = dynamic_cast<TerminalPanel*>(m_contentPanel);

        if (panel) {

            SSH_LOG("ConnectInfo: Found TerminalPanel, initializing SplitManager");

            m_splitManager->Initialize(std::shared_ptr<ISplitable>(panel, [](TerminalPanel*) {}));

            m_termCanvas = panel->GetCanvas();

            

            // Set split callback on SplitManager so new panels get it too

            m_splitManager->SetSplitCallback([this](wxSplitMode mode, TerminalPanel* sourcePanel) {

                SSH_LOG("ConnectInfo: split_callback called with mode=" << mode << " sourcePanel=" << sourcePanel);

                HandleSplit(mode, sourcePanel);

            });

            m_splitManager->SetCloseCallback([this](TerminalPanel* sourcePanel) {
                SSH_LOG("ConnectInfo: close_callback called with sourcePanel=" << sourcePanel);
                HandleClosePanel(sourcePanel);
            });

            // 閲嶆柊璁剧疆 root panel 鐨?callbacks锛堝洜涓?Initialize 鏃?callbacks 杩樻病璁剧疆锛?
            m_splitManager->ApplySplitCallbackToPanel(panel);
            SSH_LOG("ConnectInfo: root panel callbacks reapplied after SetCloseCallback");

            

            if (m_termCanvas) {

                SSH_LOG("ConnectInfo: Got canvas from TerminalPanel");

            }

        }

    } else {

        SSH_LOG("ConnectInfo: ERROR - m_contentPanel is null");

    }

    

    SSH_LOG("ConnectInfo: Found TermGLCanvas: " << m_termCanvas);



    if (!m_termCanvas) {

        SSH_LOG("ConnectInfo: TermGLCanvas not found - this is a non-terminal tab (e.g., Dashboard or RootPanel)");

        // For non-terminal tabs (like Dashboard or RootPanel), don't create TerminalThread

        return;

    }

    SSH_LOG("ConnectInfo: TermGLCanvas cast successful");


    // Calculate initial vterm size based on TermGLCanvas size and configured font

    int initialRows = 30;

    int initialCols = 80;

    if (m_termCanvas) {

        wxSize canvasSize = m_termCanvas->GetSize();



        // Get configured font size

        int fontSize = GlobalConfig::GetFontSize();

        if (fontSize == 0) fontSize = 12; // Default if not configured



        // Get DPI scale from canvas

        float dpiScale = m_termCanvas->GetDPIScale();



        // Apply x2 scaling only if DPI scaling is detected (same as in TermGLCanvas)

        int terminalFontSize = fontSize;

        if (dpiScale > 1.0f) {

            terminalFontSize = static_cast<int>(fontSize * 2);

        }

        if (terminalFontSize < 8) terminalFontSize = 8;

        if (terminalFontSize > 72) terminalFontSize = 72;



        // Calculate cell size based on font size

        int cellWidth = terminalFontSize / 2;

        int cellHeight = terminalFontSize;



        // Ensure minimum cell size

        if (cellWidth < 6) cellWidth = 6;

        if (cellHeight < 12) cellHeight = 12;



        // Account for margins (8px left/right, 4px top/bottom, DPI-scaled)

        int margin_x = static_cast<int>(8 * dpiScale);

        int margin_y = static_cast<int>(4 * dpiScale);



        int availableHeight = canvasSize.GetHeight() - margin_y * 2;

        int availableWidth = canvasSize.GetWidth() - margin_x * 2;

        if (availableHeight < 100) availableHeight = 100;

        if (availableWidth < 100) availableWidth = 100;



        initialRows = availableHeight / cellHeight;

        initialCols = availableWidth / cellWidth;

        if (initialRows < 10) initialRows = 10;

        if (initialCols < 40) initialCols = 40;

        SSH_LOG("ConnectInfo: Canvas size: " << canvasSize.GetWidth() << "x" << canvasSize.GetHeight()

                << ", Font size: " << fontSize << ", Terminal font size: " << terminalFontSize

                << ", Cell size: " << cellWidth << "x" << cellHeight

                << ", Initial vterm size calculated: " << initialRows << "x" << initialCols);

    }



    // Bind terminal-specific events BEFORE creating threads so handlers are ready

    Bind(wxEVT_TERMINAL_EXIT, &ConnectInfo::OnTerminalExit, this);



    // Set scroll callback to scroll vterm history (shared for both terminal types)

    if (m_termCanvas) {

        m_termCanvas->SetScrollCallback([this](int lines) {

            if (m_isLocalTerminal && m_localTerminalThread) {

                m_localTerminalThread->ScrollVTerm(lines);

            } else if (m_terminalThread) {

                m_terminalThread->ScrollVTerm(lines);

            }

        });



        // Set mouse callback for vi mouse mode (X10 protocol)

        m_termCanvas->SetMouseCallback([this](int row, int col, int button) {

            bool inAltScreen = m_isLocalTerminal ? (m_localTerminalThread ? m_localTerminalThread->IsInAlternateScreen() : false) : (m_terminalThread ? m_terminalThread->IsInAlternateScreen() : false);

            SSH_LOG("Mouse callback: row=" << row << ", col=" << col << ", button=" << button << ", in_alt_screen=" << inAltScreen);

            if (inAltScreen) {

                // X10 mouse protocol: \x1b[M<btn><col><row>

                // btn: 0=left, 1=middle, 2=right

                // col: column + 33 (ASCII '!' is 33)

                // row: row + 33

                char seq[6];

                seq[0] = '\x1b';

                seq[1] = '[';

                seq[2] = 'M';

                seq[3] = static_cast<char>(button + 32);

                seq[4] = static_cast<char>(col + 33);

                seq[5] = static_cast<char>(row + 33);

                std::string seq_str(seq, 6);

                SSH_LOG("Sending X10 mouse sequence: " << std::hex << (int)(unsigned char)seq[0] << " " << (int)(unsigned char)seq[1] << " " << (int)(unsigned char)seq[2] << " " << (int)(unsigned char)seq[3] << " " << (int)(unsigned char)seq[4] << " " << (int)(unsigned char)seq[5] << std::dec);

                if (m_isLocalTerminal && m_localTerminalThread) {

                    m_localTerminalThread->QueueInput(seq_str);

                } else if (m_terminalThread) {

                    m_terminalThread->QueueInput(seq_str);

                }

            }

        });

    }



    // Create appropriate thread based on terminal type

    if (m_isLocalTerminal) {
        TerminalPanel* panel = dynamic_cast<TerminalPanel*>(m_contentPanel);
        if (panel) {
            // Use the panel's EventProxy
            m_eventProxy = panel->GetEventProxy();
            m_localTerminalThread = panel->GetTerminalContainer()->GetThread();
            SSH_LOG("ConnectInfo: Using EventProxy and LocalTerminalThread from TerminalPanel");
        } else {
            // Fallback for non-TerminalPanel content (should not happen in current architecture)
            m_localTerminalThread = m_termCanvas->GetLocalTerminalThread();
            m_eventProxy = std::make_shared<EventProxy>();
            m_eventProxy->SetTarget(m_termCanvas);
            m_eventProxy->SetDamageCallback([this](int rows, int cols, int cursor_row, int cursor_col, int first_nonempty_char) {
                if (m_termCanvas) {
                    wxThreadEvent evt(wxEVT_TERMINAL_DAMAGE);
                    wxQueueEvent(m_termCanvas, evt.Clone());
                }
            });
        }

        if (m_localTerminalThread) {
            m_localTerminalThread->SetEventProxy(m_eventProxy);
            SSH_LOG("ConnectInfo: Set EventProxy on LocalTerminalThread");
        }
        
        // Set keyboard callback to send input to local thread
        if (m_termCanvas) {
            SSH_LOG("ConnectInfo: Setting key callback for local terminal");
            m_termCanvas->SetKeyCallback([this](const char* data, int length) {
                SSH_LOG("ConnectInfo key callback: len=" << length << " first=" << (int)(unsigned char)data[0]);
                if (m_localTerminalThread) {
                    m_localTerminalThread->QueueInput(std::string(data, length));
                }
            });
            
            // Set split callback
            m_termCanvas->SetSplitCallback([this](wxSplitMode mode, TerminalPanel* sourcePanel) {
                SSH_LOG("ConnectInfo: split_callback called with mode=" << mode << " sourcePanel=" << sourcePanel);
                HandleSplit(mode, sourcePanel);
            });
        }
    } else {
        // Create EventProxy for SSH terminal
        TerminalPanel* panel = dynamic_cast<TerminalPanel*>(m_contentPanel);
        if (panel) {
            m_eventProxy = panel->GetEventProxy();
            SSH_LOG("ConnectInfo: Using EventProxy from TerminalPanel for SSH");
        } else {
            m_eventProxy = std::make_shared<EventProxy>();
            m_eventProxy->SetTarget(m_termCanvas);
        }

        // Set damage callback to trigger UI refresh
        m_eventProxy->SetDamageCallback([this](int rows, int cols, int cursor_row, int cursor_col, int first_nonempty_char) {
            TerminalPanel* panel = dynamic_cast<TerminalPanel*>(m_contentPanel);
            if (panel) {
                // Send to panel
                wxThreadEvent evt(wxEVT_TERMINAL_DAMAGE);
                wxQueueEvent(panel, evt.Clone());
            } else if (m_termCanvas) {
                // Fallback to canvas
                wxThreadEvent evt(wxEVT_TERMINAL_DAMAGE);
                wxQueueEvent(m_termCanvas, evt.Clone());
            }
        });

        

        // Create TerminalThread for SSH

        m_terminalThread = new TerminalThread(m_eventProxy, initialRows, initialCols, m_deviceConfig);

        if (panel) {
            panel->SetSSHThread(m_terminalThread);
            panel->SetupCanvasConnection();
        }

        SSH_LOG("ConnectInfo: TerminalThread created");

        // Note: Key callback is now set in TerminalPanel::SetupCanvasConnection
        // which includes upload/download command detection
            
        // Set split callback
        if (m_termCanvas) {
            m_termCanvas->SetSplitCallback([this](wxSplitMode mode, TerminalPanel* sourcePanel) {
                SSH_LOG("ConnectInfo: split_callback called with mode=" << mode << " sourcePanel=" << sourcePanel);
                HandleSplit(mode, sourcePanel);
            });
        }

    }

}

void ConnectInfo::HandleClosePanel(TerminalPanel* sourcePanel) {
    SSH_LOG("ConnectInfo::HandleClosePanel called with sourcePanel=" << sourcePanel);
    
    if (!sourcePanel) {
        SSH_LOG("ERROR: sourcePanel is null");
        return;
    }
    
    m_splitManager->Close(sourcePanel);
}

void ConnectInfo::ShowFileTransferDialog() {
    SSH_LOG("ConnectInfo::ShowFileTransferDialog called");
    
    // Find the active TerminalThread (including for converted sessions)
    TerminalThread* activeThread = m_terminalThread;
    if (!activeThread && m_contentPanel) {
        TerminalPanel* panel = dynamic_cast<TerminalPanel*>(m_contentPanel);
        if (panel) {
            activeThread = panel->GetSSHThread();
        }
    }
    
    // Update our device config from the active thread (critical for direct connections / oc ssh)
    if (activeThread) {
        m_deviceConfig = activeThread->GetDeviceConfig();
        SSH_LOG("ConnectInfo::ShowFileTransferDialog: updated m_deviceConfig from active TerminalThread (username=" << m_deviceConfig.username << ", address=" << m_deviceConfig.address << ")");
    }
    
    if (!m_fileTransferDialog) {
        m_fileTransferDialog = new FileTransferDialog(this,
            TranslationHelper::Tr("fileTransfer"),
            m_deviceConfig);
    } else {
        m_fileTransferDialog->Show();
        m_fileTransferDialog->Raise();
    }
}


void ConnectInfo::Connect() {

    if (m_terminalThread) {

        if (m_terminalThread->Run() != wxTHREAD_NO_ERROR) {

            SSH_LOG("ConnectInfo: Failed to start TerminalThread");

        } else {

            SSH_LOG("ConnectInfo: TerminalThread started");

        }

    }

}



void ConnectInfo::OnTerminalDamage(wxThreadEvent& event) {

    auto t0 = std::chrono::steady_clock::now();



    // Get the appropriate thread based on terminal type

    ScreenBuffer local_buffer;
    bool has_buffer = false;

    if (m_isLocalTerminal) {

        if (!m_termCanvas || !m_localTerminalThread) {

            SSH_LOG("OnTerminalDamage: m_termCanvas or m_localTerminalThread is null");

            return;

        }

        m_localTerminalThread->CopyFrontBuffer(local_buffer);
        has_buffer = true;

    } else {

        if (!m_termCanvas || !m_terminalThread) {

            SSH_LOG("OnTerminalDamage: m_termCanvas or m_terminalThread is null");

            return;

        }

        m_terminalThread->CopyFrontBuffer(local_buffer);
        has_buffer = true;

    }



    if (!has_buffer) {

        SSH_LOG("OnTerminalDamage: buffer is null");

        return;

    }



    // SSH_LOG("OnTerminalDamage: buffer rows=" << local_buffer.rows << ", cols=" << local_buffer.cols << ", cursor=" << local_buffer.cursor_row << "," << local_buffer.cursor_col);



    // Convert entire buffer to CellInstance vector

    std::vector<CellInstance> instances;

    for (int row = 0; row < local_buffer.rows; row++) {

        for (int col = 0; col < local_buffer.cols; col++) {

            CellInstance cell = local_buffer.cells[row][col];

            cell.cell_x = (float)col;

            cell.cell_y = (float)row;

            instances.push_back(cell);

        }

    }



    // SSH_LOG("OnTerminalDamage: created " << instances.size() << " cell instances");



    m_termCanvas->ClearScreenData();

    m_termCanvas->UpdateScreenData(instances);



    // Get alternate screen state and scroll offset from appropriate thread

    bool inAltScreen = m_isLocalTerminal ? (m_localTerminalThread ? m_localTerminalThread->IsInAlternateScreen() : false) : (m_terminalThread ? m_terminalThread->IsInAlternateScreen() : false);

    int scrollOffset = m_isLocalTerminal ? (m_localTerminalThread ? m_localTerminalThread->GetScrollOffset() : 0) : (m_terminalThread ? m_terminalThread->GetScrollOffset() : 0);

    m_termCanvas->SetCursorPosition(local_buffer.cursor_row, local_buffer.cursor_col, inAltScreen, scrollOffset);

    

    // Set cursor visibility based on event

    TerminalDamageEvent* damageEvent = dynamic_cast<TerminalDamageEvent*>(&event);

    if (damageEvent) {

        m_termCanvas->SetCursorVisible(damageEvent->GetCursorVisible());

    }

    

    m_termCanvas->Refresh();

    

    auto t1 = std::chrono::steady_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    if (ms > 5) {

        SSH_LOG("PROFILE: ConnectInfo::OnTerminalDamage took " << ms << "ms");

    }

}



void ConnectInfo::OnTerminalExit(wxThreadEvent& event) {

    SSH_LOG("ConnectInfo: TerminalThread exited");

    // Thread cleanup is handled by wxThread

}



void ConnectInfo::OnFileTransferRequest(wxCommandEvent& event) {
    // Debug log (disabled)
    // std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
    // if (f.is_open()) f << "[CONNECTINFO] OnFileTransferRequest called, event.GetInt()=" << event.GetInt() << std::endl;
    
    // Handle upload/download command detection from SSH terminal
    if (event.GetInt() == 2) {
        wxString command = event.GetString();
        SSH_LOG("File transfer request for command: " << command.ToStdString());
        // if (f.is_open()) f << "[CONNECTINFO] Calling ShowFileTransferDialog for command: " << command.ToStdString() << std::endl;
        ShowFileTransferDialog();
        return;
    }

    wxString json = event.GetString();

    SSH_LOG("File Transfer Request: " << json);

    

    try {

        nlohmann::json j = nlohmann::json::parse(json.ToStdString());

        std::string deviceId = j["device_id"];

        

        // Check if the device ID matches this ConnectInfo

        if (deviceId != m_deviceConfig.id) {

            SSH_LOG("Device ID mismatch: " << deviceId << " != " << m_deviceConfig.id);

            return;

        }

        

        // Check if file transfer thread exists and is running

        if (!m_fileTransferThread || !m_fileTransferThread->IsRunning()) {

            // Create new thread

            if (m_fileTransferThread) {

                delete m_fileTransferThread;

            }

            m_fileTransferThread = new FileTransferThread(this, m_deviceConfig);

            if (m_fileTransferThread->Run() != wxTHREAD_NO_ERROR) {

                SSH_LOG("Failed to start FileTransferThread");

                delete m_fileTransferThread;

                m_fileTransferThread = nullptr;

                return;

            }

            SSH_LOG("FileTransferThread started for device: " << deviceId);

        }

        

        // Parse task and add to thread

        FileTransferTask task = FileTransferTask::fromJson(j);

        task.status = "pending";

        task.progress = 0;

        task.result = "";

        

        m_fileTransferThread->AddTask(task);

        SSH_LOG("Task added to queue: " << task.id);

        

    } catch (const std::exception& e) {

        SSH_LOG("Failed to parse file transfer request: " << e.what());

    }

}



void ConnectInfo::OnFileTransferProgress(wxCommandEvent& event) {

    wxString json = event.GetString();

    SSH_LOG("File Transfer Progress: " << json);

    

    // Forward event to FileTransferDialog if it exists

    if (m_fileTransferDialog) {

        SSH_LOG("Forwarding progress event to FileTransferDialog");

        wxQueueEvent(m_fileTransferDialog, event.Clone());

    } else {

        SSH_LOG("FileTransferDialog is null, cannot forward event");

    }

}



void ConnectInfo::OnFileTransferComplete(wxCommandEvent& event) {

    wxString json = event.GetString();

    SSH_LOG("File Transfer Complete: " << json);

    

    // Forward event to FileTransferDialog if it exists

    if (m_fileTransferDialog) {

        SSH_LOG("Forwarding complete event to FileTransferDialog");

        wxQueueEvent(m_fileTransferDialog, event.Clone());

    } else {

        SSH_LOG("FileTransferDialog is null, cannot forward event");

    }

}



void ConnectInfo::UpdateVTermSize(int rows, int cols) {

    // This method is no longer needed with TerminalThread architecture

    // Resize is handled in OnSize by the thread itself

    SSH_LOG("ConnectInfo::UpdateVTermSize called - deprecated in thread architecture");

}



void ConnectInfo::SetActive(bool active) {

    m_isActive = active;

    Refresh();

}



wxWindow* ConnectInfo::GetContentPanel() const {

    return m_contentPanel;

}



DeviceConfig ConnectInfo::GetDeviceConfig() const {

    return m_deviceConfig;

}



int ConnectInfo::GetPreferredWidth() const {

    double dpiScale = 1.0;

    if (GetHandle()) {

        dpiScale = GetDPIScaleFactor();

    }

    if (dpiScale <= 0.0) dpiScale = 1.0;



    wxClientDC dc(const_cast<ConnectInfo*>(this));

    wxSize textSize = dc.GetTextExtent(m_label->GetLabel());

    

    int slant = 10;

    int padding = 12;



    if (m_closeButton && m_closeButton->IsShown()) {

        // Use actual button size instead of hardcoded value
        int closeButtonWidth = m_closeButton->GetSize().GetWidth();
        if (closeButtonWidth <= 0) {
            // Fallback to hardcoded size if button size is not available
            int baseCloseButtonSize = 20;
#ifdef __APPLE__
            closeButtonWidth = baseCloseButtonSize;
#else
            closeButtonWidth = static_cast<int>(baseCloseButtonSize * dpiScale);
#endif
        }

        // Left slant + Left padding + text + middle spacing (e.g. 15px minimum) + close btn + Right padding + Right slant

        m_cachedWidth = slant + padding + textSize.GetWidth() + 15 + closeButtonWidth + padding + slant;

    } else {

        // No close button (e.g. Home tab)

        m_cachedWidth = slant + padding + textSize.GetWidth() + padding + slant;

    }

    return m_cachedWidth;

}



void ConnectInfo::OnPaint(wxPaintEvent& event)

{

    wxPaintDC dc(this);

    wxSize size = GetSize();

    int slant = 10;



    wxPoint points[4];

    points[0] = wxPoint(0, size.y);

    points[1] = wxPoint(slant, 0);

    points[2] = wxPoint(size.x - slant, 0);

    points[3] = wxPoint(size.x, size.y);



    wxColour backgroundColour;

    wxColour borderColour = wxColour(20, 20, 20);



    if (m_isActive) {

        backgroundColour = wxColour(45, 45, 45);

    } else if (m_isHovered) {

        backgroundColour = wxColour(60, 60, 60);

    } else {

        backgroundColour = wxColour(30, 30, 30);

    }



    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));

    dc.Clear();



    dc.SetBrush(wxBrush(backgroundColour));

    dc.SetPen(wxPen(borderColour, 1));

    dc.DrawPolygon(4, points);



    // Draw bottom line to ensure it extends to the bottom edge

    dc.SetPen(wxPen(borderColour, 1));

    dc.DrawLine(0, size.y - 1, size.x, size.y - 1);



    m_label->SetBackgroundColour(backgroundColour);

    m_label->SetForegroundColour(*wxWHITE);

    m_closeButton->SetBackgroundColour(backgroundColour);

    m_closeButton->SetForegroundColour(*wxWHITE);



    event.Skip();

}



void ConnectInfo::OnEnter(wxMouseEvent& event) {

    m_isHovered = true;

    Refresh();

}



void ConnectInfo::OnLeave(wxMouseEvent& event) {

    m_isHovered = false;

    Refresh();

}



void ConnectInfo::OnClose(wxCommandEvent& event) {

    // Stop event propagation to prevent parent from handling the button click

    event.StopPropagation();



    // Send close event with this tab as the event object

    wxCommandEvent closeEvent(wxEVT_TAB_CLOSE, GetId());

    closeEvent.SetEventObject(this);

    wxPostEvent(GetParent(), closeEvent);

}



void ConnectInfo::OnSelected(wxMouseEvent& event) {

    wxCommandEvent selectedEvent(wxEVT_TAB_SELECTED, GetId());

    selectedEvent.SetEventObject(this);

    wxPostEvent(GetParent(), selectedEvent);

}

void ConnectInfo::OnLabelDoubleClick(wxMouseEvent& event) {
    event.Skip();
    
    // Store current label for rollback if empty
    wxString currentLabel = m_label->GetLabel();
    
    // Get label position and size
    wxRect labelRect = m_label->GetRect();
    
    // Create text editor
    m_labelEditor = new wxTextCtrl(this, wxID_ANY, currentLabel, 
                                   labelRect.GetPosition(), labelRect.GetSize(),
                                   wxTE_PROCESS_ENTER | wxBORDER_NONE);
    
    // Match label appearance
    m_labelEditor->SetForegroundColour(*wxWHITE);
    m_labelEditor->SetBackgroundColour(m_isActive ? wxColour(45, 45, 45) : wxColour(30, 30, 30));
    wxFont labelFont = m_label->GetFont();
    m_labelEditor->SetFont(labelFont);
    
    // Hide label and show editor
    m_label->Hide();
    m_labelEditor->Show();
    m_labelEditor->SetFocus();
    m_labelEditor->SelectAll();
    
    // Bind events
    m_labelEditor->Bind(wxEVT_TEXT_ENTER, &ConnectInfo::OnLabelTextEnter, this);
    m_labelEditor->Bind(wxEVT_KILL_FOCUS, &ConnectInfo::OnLabelTextKillFocus, this);
}

void ConnectInfo::OnLabelTextEnter(wxCommandEvent& event) {
    if (m_labelEditor) {
        wxString newLabel = m_labelEditor->GetValue().Trim();
        wxString oldLabel = m_label->GetLabel();
        
        // If empty, use old label
        if (newLabel.IsEmpty()) {
            newLabel = oldLabel;
        }
        
        // Update label
        m_label->SetLabel(newLabel);
        
        // Cleanup editor
        m_labelEditor->Unbind(wxEVT_TEXT_ENTER, &ConnectInfo::OnLabelTextEnter, this);
        m_labelEditor->Unbind(wxEVT_KILL_FOCUS, &ConnectInfo::OnLabelTextKillFocus, this);
        m_labelEditor->Destroy();
        m_labelEditor = nullptr;
        
        // Show label
        m_label->Show();
        
        // Update notebook page text directly to avoid deadlock
        wxWindow* parent = GetParent();
        if (parent) {
            wxSimplebook* notebook = nullptr;
            
            // Find the notebook by walking up the parent chain
            wxWindow* current = parent;
            while (current) {
                notebook = dynamic_cast<wxSimplebook*>(current);
                if (notebook) break;
                current = current->GetParent();
            }
            
            if (notebook && m_contentPanel) {
                // Find the notebook page index for this content panel
                int pageIndex = wxNOT_FOUND;
                for (size_t i = 0; i < notebook->GetPageCount(); ++i) {
                    if (notebook->GetPage(i) == m_contentPanel) {
                        pageIndex = i;
                        break;
                    }
                }
                
                if (pageIndex != wxNOT_FOUND) {
                    notebook->SetPageText(pageIndex, newLabel);
                }
            }
            
            // Notify parent to relayout tabs
            wxCommandEvent layoutEvent(wxEVT_COMMAND_MENU_SELECTED, wxID_ANY);
            layoutEvent.SetInt(1); // Flag to indicate tab label changed
            wxPostEvent(parent, layoutEvent);
        }
    }
}

void ConnectInfo::OnLabelTextKillFocus(wxFocusEvent& event) {
    // Treat focus loss same as Enter
    wxCommandEvent cmdEvent;
    OnLabelTextEnter(cmdEvent);
    event.Skip();
}



void ConnectInfo::OnSize(wxSizeEvent& event) {
    SSH_LOG("ConnectInfo::OnSize called");
    event.Skip();
}



void ConnectInfo::HandleSplit(wxSplitMode mode, TerminalPanel* sourcePanel) {

    SSH_LOG("ConnectInfo::HandleSplit called with mode=" << mode << " sourcePanel=" << sourcePanel);

    

    SplitTree* tree = m_splitManager->GetTree();

    if (!tree || !tree->GetRoot()) {

        SSH_LOG("ERROR: SplitTree is empty");

        return;

    }

    

    TerminalPanel* targetPanel = sourcePanel;

    

    if (targetPanel) {

        SSH_LOG("HandleSplit: using sourcePanel from callback=" << targetPanel);

    } else {

        // Fallback: find first leaf

        std::function<SplitNode*(SplitNode*)> findFirstLeaf = [&](SplitNode* node) -> SplitNode* {

            if (!node) return nullptr;

            if (node->type == SplitNodeType::Leaf) return node;

            SplitNode* found = findFirstLeaf(node->left.get());

            if (found) return found;

            return findFirstLeaf(node->right.get());

        };

        SplitNode* firstLeaf = findFirstLeaf(tree->GetRoot());

        if (firstLeaf) {

            targetPanel = dynamic_cast<TerminalPanel*>(firstLeaf->content.get());

            SSH_LOG("HandleSplit: fallback to first leaf panel=" << targetPanel);

        }

    }

    

    if (targetPanel) {

        SSH_LOG("Splitting target panel using SplitManager");

        m_splitManager->Split(targetPanel, mode);

    } else {

        SSH_LOG("ERROR: Could not find target panel to split");

    }

}



void ConnectInfo::SwitchToSSH(TerminalThread* sshThread, const DeviceConfig& deviceConfig) {
    SSH_LOG("ConnectInfo::SwitchToSSH called");
    m_isLocalTerminal = false;
    m_localTerminalThread = nullptr;
    m_terminalThread = sshThread;
    m_deviceConfig = deviceConfig;

    // Update label to show the device username@address
    wxString newLabel = wxString::FromUTF8(deviceConfig.username.c_str()) + "@" + wxString::FromUTF8(deviceConfig.address.c_str());
    m_label->SetLabel(newLabel);

    // Refresh the tab layout
    Layout();
}



ConnectInfo::~ConnectInfo() {

    // Clear raw pointers to prevent use-after-free during member destruction
    // These are owned by wxWidgets and will be destroyed automatically
    // Do NOT call any methods on them as they may already be deleted
    m_termCanvas = nullptr;
    m_contentPanel = nullptr;
    m_label = nullptr;
    m_closeButton = nullptr;
    m_terminalThread = nullptr;
    m_localTerminalThread = nullptr;

}

