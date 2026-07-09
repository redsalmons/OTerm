#pragma once



#include <wx/wx.h>

#include <wx/splitter.h>

#include "DeviceConfig.h"

#include "ScreenBuffer.h"

#include "FileTransferDialog.h"

#include "EventProxy.h"

#include <memory>

#include <chrono>

#include <map>



class TermGLCanvas;

class TerminalThread;

class TerminalPanel;

class InfiniteSplitter;

class SplitManager;



wxDECLARE_EVENT(wxEVT_TAB_CLOSE, wxCommandEvent);

wxDECLARE_EVENT(wxEVT_TAB_SELECTED, wxCommandEvent);



class ConnectInfo : public wxPanel {

public:

    ConnectInfo(wxWindow* parent, const wxString& label, wxWindow* contentPanel, const DeviceConfig& deviceConfig, bool showCloseButton = true, bool isLocalTerminal = false);



    void SetActive(bool active);

    bool IsActive() const { return m_isActive; }

    bool IsLocalTerminal() const { return m_isLocalTerminal; }

    wxWindow* GetContentPanel() const;

    void SetContentPanel(wxWindow* panel) { m_contentPanel = panel; }

    DeviceConfig GetDeviceConfig() const;

    TermGLCanvas* GetCanvas() const { return m_termCanvas; }

    wxString GetLabel() const { return m_label->GetLabel(); }

    int GetPreferredWidth() const;

    int GetCachedWidth() const { return m_cachedWidth; }



    void Connect();

    TerminalThread* GetTerminalThread() const { return m_terminalThread; }

    void SwitchToSSH(const DeviceConfig& deviceConfig);



    void UpdateVTermSize(int rows, int cols);

    

    // Split functionality

    SplitManager* GetSplitManager() const { return m_splitManager.get(); }

    void HandleSplit(wxSplitMode mode, TerminalPanel* sourcePanel = nullptr);

    void HandleClosePanel(TerminalPanel* sourcePanel);

    void ShowFileTransferDialog();

    

    ~ConnectInfo();



private:

    void OnPaint(wxPaintEvent& event);

    void OnEnter(wxMouseEvent& event);

    void OnLeave(wxMouseEvent& event);

    void OnClose(wxCommandEvent& event);

    void OnSelected(wxMouseEvent& event);

    void OnLabelDoubleClick(wxMouseEvent& event);

    void OnLabelTextEnter(wxCommandEvent& event);

    void OnLabelTextKillFocus(wxFocusEvent& event);

    void OnSize(wxSizeEvent& event);

    void OnFileTransferRequest(wxCommandEvent& event);

    void OnFileTransferProgress(wxCommandEvent& event);

    void OnFileTransferComplete(wxCommandEvent& event);

    void UpdateTaskFile(const wxString& taskId, int progress, const wxString& status, const wxString& result = "");



    wxStaticText* m_label;

    wxTextCtrl* m_labelEditor;

    wxButton* m_closeButton;

    wxWindow* m_contentPanel;

    DeviceConfig m_deviceConfig;

    bool m_isActive;

    bool m_isHovered;

    bool m_isLocalTerminal;



    TerminalThread* m_terminalThread;

    TermGLCanvas* m_termCanvas;

    EventProxyPtr m_eventProxy;

    std::string m_currentInput; // Record current keyboard input

    FileTransferDialog* m_fileTransferDialog; // File transfer dialog
    
    std::map<wxString, std::chrono::steady_clock::time_point> m_lastTaskUpdateTime; // Track last update time for throttling

    int m_prevRows;

    int m_prevCols;

    mutable int m_cachedWidth;

    std::unique_ptr<SplitManager> m_splitManager;

};

