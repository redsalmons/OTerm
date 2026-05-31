#pragma once

#include <wx/wx.h>
#include "DeviceConfig.h"
#include "ScreenBuffer.h"
#include "FileTransferDialog.h"
#include "FileTransferThread.h"

class TermGLCanvas;
class TerminalThread;

wxDECLARE_EVENT(wxEVT_TAB_CLOSE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_TAB_SELECTED, wxCommandEvent);

class ConnectInfo : public wxPanel {
public:
    ConnectInfo(wxWindow* parent, const wxString& label, wxWindow* contentPanel, const DeviceConfig& deviceConfig, bool showCloseButton = true);

    void SetActive(bool active);
    bool IsActive() const { return m_isActive; }
    wxWindow* GetContentPanel() const;
    DeviceConfig GetDeviceConfig() const;
    TermGLCanvas* GetCanvas() const { return m_termCanvas; }
    wxString GetLabel() const { return m_label->GetLabel(); }

    void Connect();
    TerminalThread* GetTerminalThread() const { return m_terminalThread; }

    void UpdateVTermSize(int rows, int cols);

private:
    void OnPaint(wxPaintEvent& event);
    void OnEnter(wxMouseEvent& event);
    void OnLeave(wxMouseEvent& event);
    void OnClose(wxCommandEvent& event);
    void OnSelected(wxMouseEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnTerminalDamage(wxThreadEvent& event);
    void OnTerminalExit(wxThreadEvent& event);
    void OnFileTransferRequest(wxCommandEvent& event);
    void OnFileTransferProgress(wxCommandEvent& event);
    void OnFileTransferComplete(wxCommandEvent& event);

    wxStaticText* m_label;
    wxButton* m_closeButton;
    wxWindow* m_contentPanel;
    DeviceConfig m_deviceConfig;
    bool m_isActive;
    bool m_isHovered;

    TerminalThread* m_terminalThread;
    TermGLCanvas* m_termCanvas;
    std::string m_currentInput; // Record current keyboard input
    FileTransferDialog* m_fileTransferDialog; // File transfer dialog
    FileTransferThread* m_fileTransferThread; // File transfer thread
};
