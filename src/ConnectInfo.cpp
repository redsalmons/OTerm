#include "ConnectInfo.h"
#include "TermGLCanvas.h"
#include "TerminalThread.h"
#include "SSHManager.h"
#include "FileTransferTask.h"
#include "TranslationHelper.h"
#include <wx/display.h>
#include <nlohmann/json.hpp>

wxDEFINE_EVENT(wxEVT_TAB_CLOSE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_TAB_SELECTED, wxCommandEvent);

ConnectInfo::ConnectInfo(wxWindow* parent, const wxString& label, wxWindow* contentPanel, const DeviceConfig& deviceConfig, bool showCloseButton)
    : wxPanel(parent, wxID_ANY), m_contentPanel(contentPanel), m_deviceConfig(deviceConfig),
      m_isActive(false), m_isHovered(false), m_terminalThread(nullptr), m_termCanvas(nullptr),
      m_fileTransferDialog(nullptr), m_fileTransferThread(nullptr) {
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
    
    // Set tab height based on DPI scale
    int baseTabHeight = 35;
    int scaledTabHeight = static_cast<int>(baseTabHeight * dpiScale);
    SetMinSize(wxSize(120, scaledTabHeight));
    
    m_label = new wxStaticText(this, wxID_ANY, label);
    m_label->SetForegroundColour(*wxWHITE);
    m_label->Refresh();
    int baseCloseButtonSize = 20;
    int scaledCloseButtonSize = static_cast<int>(baseCloseButtonSize * dpiScale);
    m_closeButton = new wxButton(this, wxID_ANY, "x", wxDefaultPosition, wxSize(scaledCloseButtonSize, scaledCloseButtonSize), wxBORDER_NONE);
    m_closeButton->Show(showCloseButton);

    wxBoxSizer* h_sizer = new wxBoxSizer(wxHORIZONTAL);
    h_sizer->AddStretchSpacer();
    h_sizer->Add(m_label, 0, wxALIGN_CENTER);
    if (showCloseButton) {
        h_sizer->Add(m_closeButton, 0, wxALIGN_CENTER | wxLEFT, 5);
    }
    h_sizer->AddStretchSpacer();

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    v_sizer->Add(h_sizer, 1, wxALIGN_CENTER);
    SetSizer(v_sizer);

    // Bind events for all tabs (including non-terminal tabs like Dashboard)
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &ConnectInfo::OnPaint, this);
    Bind(wxEVT_ENTER_WINDOW, &ConnectInfo::OnEnter, this);
    Bind(wxEVT_LEAVE_WINDOW, &ConnectInfo::OnLeave, this);
    m_closeButton->Bind(wxEVT_BUTTON, &ConnectInfo::OnClose, this);
    Bind(wxEVT_LEFT_DOWN, &ConnectInfo::OnSelected, this);
    m_label->Bind(wxEVT_LEFT_DOWN, &ConnectInfo::OnSelected, this);
    Bind(wxEVT_SIZE, &ConnectInfo::OnSize, this);
    Bind(wxEVT_FILE_TRANSFER_REQUEST, &ConnectInfo::OnFileTransferRequest, this);
    Bind(wxEVT_FILE_TRANSFER_PROGRESS, &ConnectInfo::OnFileTransferProgress, this);
    Bind(wxEVT_FILE_TRANSFER_COMPLETE, &ConnectInfo::OnFileTransferComplete, this);

    // Cast contentPanel to TermGLCanvas
    m_termCanvas = dynamic_cast<TermGLCanvas*>(m_contentPanel);
    if (!m_termCanvas) {
        SSH_LOG("ConnectInfo: TermGLCanvas cast failed - this is a non-terminal tab (e.g., Dashboard)");
        // For non-terminal tabs (like Dashboard), don't create TerminalThread
        return;
    }
    SSH_LOG("ConnectInfo: TermGLCanvas cast successful");

    // Calculate initial vterm size based on TermGLCanvas size
    int initialRows = 30;
    int initialCols = 80;
    if (m_termCanvas) {
        wxSize canvasSize = m_termCanvas->GetSize();
        int cellWidth = 12;
        int cellHeight = 24;
        initialRows = canvasSize.GetHeight() / cellHeight;
        initialCols = canvasSize.GetWidth() / cellWidth;
        if (initialRows < 10) initialRows = 10;
        if (initialCols < 40) initialCols = 40;
        SSH_LOG("ConnectInfo: Initial vterm size calculated: " << initialRows << "x" << initialCols);
    }

    // Create TerminalThread
    m_terminalThread = new TerminalThread(this, initialRows, initialCols, m_deviceConfig);
    SSH_LOG("ConnectInfo: TerminalThread created");

    // Set keyboard callback to send input to thread
    if (m_termCanvas) {
        m_termCanvas->SetKeyCallback([this](const char* data, int length) {
            if (m_terminalThread) {
                // Process keyboard input
                std::string modified_data;
                
                for (int i = 0; i < length; i++) {
                    if (data[i] == '\r' || data[i] == '\n') {
                        // Enter key pressed - check if command is download or upload
                        if (!m_currentInput.empty()) {
                            SSH_LOG("Command entered: " << m_currentInput);
                            
                            // If command is "download" or "upload", show file transfer dialog
                            if (m_currentInput == "download" || m_currentInput == "upload") {
                                modified_data += '\x03';
                                SSH_LOG("Detected download/upload command, showing file transfer dialog");
                                
                                // Show file transfer dialog (non-modal)
                                if (!m_fileTransferDialog) {
                                    m_fileTransferDialog = new FileTransferDialog(this,
                                        TranslationHelper::Tr("fileTransfer"),
                                        m_deviceConfig);
                                } else {
                                    m_fileTransferDialog->Show();
                                    m_fileTransferDialog->Raise();
                                }
                            } else {
                                modified_data += data[i];
                            }
                            
                            m_currentInput.clear();
                        }
                    } else if (data[i] == 127 || data[i] == 8) {
                        // Backspace - remove last character
                        if (!m_currentInput.empty()) {
                            m_currentInput.pop_back();
                        }
                        modified_data += data[i];
                    } else if (data[i] >= 32 && data[i] < 127) {
                        // Printable character
                        m_currentInput += data[i];
                        modified_data += data[i];
                    } else {
                        // Other control characters
                        modified_data += data[i];
                    }
                }
                
                // Send to SSH
                if (!modified_data.empty()) {
                    m_terminalThread->QueueInput(modified_data);
                }
            }
        });
        
        // Set scroll callback to scroll vterm history
        m_termCanvas->SetScrollCallback([this](int lines) {
            if (m_terminalThread) {
                m_terminalThread->ScrollVTerm(lines);
            }
        });
    }

    // Bind terminal-specific events only for terminal tabs
    Bind(wxEVT_TERMINAL_DAMAGE, &ConnectInfo::OnTerminalDamage, this);
    Bind(wxEVT_TERMINAL_EXIT, &ConnectInfo::OnTerminalExit, this);
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
    SSH_LOG("OnTerminalDamage called");
    if (!m_termCanvas || !m_terminalThread) {
        SSH_LOG("OnTerminalDamage: m_termCanvas or m_terminalThread is null");
        return;
    }
    
    const ScreenBuffer* buffer = m_terminalThread->GetFrontBuffer();
    if (!buffer) {
        SSH_LOG("OnTerminalDamage: buffer is null");
        return;
    }
    
    SSH_LOG("OnTerminalDamage: buffer rows=" << buffer->rows << ", cols=" << buffer->cols << ", cursor=" << buffer->cursor_row << "," << buffer->cursor_col);
    
    // Convert entire buffer to CellInstance vector
    std::vector<CellInstance> instances;
    for (int row = 0; row < buffer->rows; row++) {
        for (int col = 0; col < buffer->cols; col++) {
            CellInstance cell = buffer->cells[row][col];
            cell.cell_x = (float)col;
            cell.cell_y = (float)row;
            instances.push_back(cell);
        }
    }
    
    SSH_LOG("OnTerminalDamage: created " << instances.size() << " cell instances");
    
    m_termCanvas->ClearScreenData();
    m_termCanvas->UpdateScreenData(instances);
    m_termCanvas->SetCursorPosition(buffer->cursor_row, buffer->cursor_col);
    
    // Set cursor visibility based on event
    TerminalDamageEvent* damageEvent = dynamic_cast<TerminalDamageEvent*>(&event);
    if (damageEvent) {
        m_termCanvas->SetCursorVisible(damageEvent->GetCursorVisible());
    }
    
    m_termCanvas->Refresh();
    SSH_LOG("OnTerminalDamage: Refresh called");
}

void ConnectInfo::OnTerminalExit(wxThreadEvent& event) {
    SSH_LOG("ConnectInfo: TerminalThread exited");
    // Thread cleanup is handled by wxThread
}

void ConnectInfo::OnFileTransferRequest(wxCommandEvent& event) {
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
    // TODO: Update UI to show progress
}

void ConnectInfo::OnFileTransferComplete(wxCommandEvent& event) {
    wxString json = event.GetString();
    SSH_LOG("File Transfer Complete: " << json);
    // TODO: Update UI to show completion
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
    wxCommandEvent closeEvent(wxEVT_TAB_CLOSE, GetId());
    closeEvent.SetEventObject(this);
    wxPostEvent(GetParent(), closeEvent);
}

void ConnectInfo::OnSelected(wxMouseEvent& event) {
    wxCommandEvent selectedEvent(wxEVT_TAB_SELECTED, GetId());
    selectedEvent.SetEventObject(this);
    wxPostEvent(GetParent(), selectedEvent);
}

void ConnectInfo::OnSize(wxSizeEvent& event) {
    SSH_LOG("ConnectInfo::OnSize called");
    if (m_termCanvas && m_terminalThread) {
        wxSize size = m_termCanvas->GetSize();
        
        // Calculate rows and columns based on cell size
        int cellWidth = 12;
        int cellHeight = 24;
        
        int availableHeight = size.GetHeight();
        if (availableHeight < 100) availableHeight = 100;
        
        int rows = availableHeight / cellHeight;
        int cols = size.GetWidth() / cellWidth;
        
        // Ensure minimum size
        if (rows < 10) rows = 10;
        if (cols < 40) cols = 40;
        
        // Track previous vterm size to avoid unnecessary resizes
        static int prevRows = 0;
        static int prevCols = 0;
        
        // Only resize if vterm size actually changed
        if (rows != prevRows || cols != prevCols) {
            SSH_LOG("ConnectInfo::OnSize - TermGLCanvas size: " << size.GetWidth() << "x" << size.GetHeight()
                    << ", Calculated vterm: " << rows << "x" << cols);
            
            // Resize vterm in thread
            // The thread will send damage events after resize completes
            // and automatically reset scroll to bottom
            m_terminalThread->ResizeVTerm(rows, cols);
            
            prevRows = rows;
            prevCols = cols;
        }
    }
    event.Skip();
}
