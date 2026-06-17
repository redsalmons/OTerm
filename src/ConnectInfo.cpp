#include "ConnectInfo.h"
#include "TermGLCanvas.h"
#include "TerminalThread.h"
#include "LocalTerminalThread.h"
#include "SSHManager.h"
#include "FileTransferTask.h"
#include "TranslationHelper.h"
#include "GlobalConfig.h"
#include <wx/display.h>
#include <nlohmann/json.hpp>

wxDEFINE_EVENT(wxEVT_TAB_CLOSE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_TAB_SELECTED, wxCommandEvent);

ConnectInfo::ConnectInfo(wxWindow* parent, const wxString& label, wxWindow* contentPanel, const DeviceConfig& deviceConfig, bool showCloseButton, bool isLocalTerminal)
    : wxPanel(parent, wxID_ANY), m_contentPanel(contentPanel), m_deviceConfig(deviceConfig),
      m_isActive(false), m_isHovered(false), m_isLocalTerminal(isLocalTerminal), m_terminalThread(nullptr), m_localTerminalThread(nullptr), m_termCanvas(nullptr),
      m_fileTransferDialog(nullptr), m_fileTransferThread(nullptr), m_prevRows(0), m_prevCols(0) {
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
    if (!showCloseButton) {
        // Homepage tab: add left/right stretch spacers to center label horizontally
        h_sizer->AddStretchSpacer(1);
        h_sizer->Add(m_label, 0, wxALIGN_CENTER_VERTICAL);
        h_sizer->AddStretchSpacer(1);
    } else {
        h_sizer->Add(m_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
        h_sizer->Add(m_closeButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
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

    // Bind canvas resize to update terminal size (essential for window height/width changes)
    m_termCanvas->Bind(wxEVT_SIZE, &ConnectInfo::OnSize, this);

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

    // Create appropriate thread based on terminal type
    if (m_isLocalTerminal) {
        // Create LocalTerminalThread for local shell
        m_localTerminalThread = new LocalTerminalThread(this, initialRows, initialCols);
        m_localTerminalThread->Start();
        SSH_LOG("ConnectInfo: LocalTerminalThread created and started");

        // Set keyboard callback to send input to local thread
        if (m_termCanvas) {
            m_termCanvas->SetKeyCallback([this](const char* data, int length) {
                if (m_localTerminalThread) {
                    m_localTerminalThread->QueueInput(std::string(data, length));
                }
            });
        }
    } else {
        // Create TerminalThread for SSH
        m_terminalThread = new TerminalThread(this, initialRows, initialCols, m_deviceConfig);
        SSH_LOG("ConnectInfo: TerminalThread created");

        // Set keyboard callback to send input to thread
        if (m_termCanvas) {
            m_termCanvas->SetKeyCallback([this](const char* data, int length) {
                if (m_terminalThread) {
                    if (length >= 2 && data[0] == '\x1b') {
                        m_terminalThread->QueueInput(std::string(data, length));
                        return;
                    }

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
                            } else {
                                modified_data += data[i];
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
        }
    }

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
    // SSH_LOG("OnTerminalDamage called");

    // Get the appropriate thread based on terminal type
    const ScreenBuffer* buffer = nullptr;
    if (m_isLocalTerminal) {
        if (!m_termCanvas || !m_localTerminalThread) {
            SSH_LOG("OnTerminalDamage: m_termCanvas or m_localTerminalThread is null");
            return;
        }
        buffer = m_localTerminalThread->GetFrontBuffer();
    } else {
        if (!m_termCanvas || !m_terminalThread) {
            SSH_LOG("OnTerminalDamage: m_termCanvas or m_terminalThread is null");
            return;
        }
        buffer = m_terminalThread->GetFrontBuffer();
    }

    if (!buffer) {
        SSH_LOG("OnTerminalDamage: buffer is null");
        return;
    }

    // SSH_LOG("OnTerminalDamage: buffer rows=" << buffer->rows << ", cols=" << buffer->cols << ", cursor=" << buffer->cursor_row << "," << buffer->cursor_col);

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

    // SSH_LOG("OnTerminalDamage: created " << instances.size() << " cell instances");

    m_termCanvas->ClearScreenData();
    m_termCanvas->UpdateScreenData(instances);

    // Get alternate screen state and scroll offset from appropriate thread
    bool inAltScreen = m_isLocalTerminal ? (m_localTerminalThread ? m_localTerminalThread->IsInAlternateScreen() : false) : (m_terminalThread ? m_terminalThread->IsInAlternateScreen() : false);
    int scrollOffset = m_isLocalTerminal ? (m_localTerminalThread ? m_localTerminalThread->GetScrollOffset() : 0) : (m_terminalThread ? m_terminalThread->GetScrollOffset() : 0);
    m_termCanvas->SetCursorPosition(buffer->cursor_row, buffer->cursor_col, inAltScreen, scrollOffset);
    
    // Set cursor visibility based on event
    TerminalDamageEvent* damageEvent = dynamic_cast<TerminalDamageEvent*>(&event);
    if (damageEvent) {
        m_termCanvas->SetCursorVisible(damageEvent->GetCursorVisible());
    }
    
    m_termCanvas->Refresh();
    // SSH_LOG("OnTerminalDamage: Refresh called");
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

void ConnectInfo::OnSize(wxSizeEvent& event) {
    SSH_LOG("ConnectInfo::OnSize called");
    if (m_termCanvas) {
        wxSize size = m_termCanvas->GetSize();

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

        // Calculate cell size based on font size (use actual dimensions if TermGLCanvas has loaded them)
        int cellWidth = (m_termCanvas->m_cellWidth > 0) ? m_termCanvas->m_cellWidth : (terminalFontSize / 2);
        int cellHeight = (m_termCanvas->m_cellHeight > 0) ? m_termCanvas->m_cellHeight : terminalFontSize;

        // Ensure minimum cell size
        if (cellWidth < 6) cellWidth = 6;
        if (cellHeight < 12) cellHeight = 12;

        // Account for margins (8px left/right, 4px top/bottom, DPI-scaled)
        int margin_x = static_cast<int>(8 * dpiScale);
        int margin_y = static_cast<int>(4 * dpiScale);

        int availableHeight = size.GetHeight() - margin_y * 2;
        int availableWidth = size.GetWidth() - margin_x * 2;
        if (availableHeight < 100) availableHeight = 100;
        if (availableWidth < 100) availableWidth = 100;

        int rows = availableHeight / cellHeight;
        int cols = availableWidth / cellWidth;
        
        // Ensure minimum size
        if (rows < 10) rows = 10;
        if (cols < 40) cols = 40;

        // Only resize if vterm size actually changed
        if (rows != m_prevRows || cols != m_prevCols) {
            SSH_LOG("ConnectInfo::OnSize - TermGLCanvas size: " << size.GetWidth() << "x" << size.GetHeight()
                    << ", Calculated vterm: " << rows << "x" << cols);

            // Resize vterm in appropriate thread
            if (m_isLocalTerminal && m_localTerminalThread) {
                m_localTerminalThread->ResizeVTerm(rows, cols);
            } else if (m_terminalThread) {
                m_terminalThread->ResizeVTerm(rows, cols);
            }

            m_prevRows = rows;
            m_prevCols = cols;
        }
    }
    event.Skip();
}

ConnectInfo::~ConnectInfo() {
    // Set shutdown flag before cleanup to prevent crash
    if (m_terminalThread) {
        m_terminalThread->SetShuttingDown();
    }
    if (m_localTerminalThread) {
        m_localTerminalThread->SetShuttingDown();
    }
}
