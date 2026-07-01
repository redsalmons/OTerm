#ifndef TERMINAL_PANEL_H
#define TERMINAL_PANEL_H

#include <wx/wx.h>
#include <wx/splitter.h>
#include "LocalTerminalContainer.h"
#include "TermGLCanvas.h"
#include "EventProxy.h"
#include "ISplitable.h"
#include "CommandInterceptor.h"

class InfiniteSplitter;

class TerminalPanel : public wxPanel, public ISplitable {
public:
    TerminalPanel(wxWindow* parent, std::unique_ptr<LocalTerminalContainer> container = nullptr);
    ~TerminalPanel();
    
    // ISplitable interface implementation
    wxWindow* GetWindow() override { return this; }
    void Shutdown() override;
    bool CanSplit() const override { return HasTerminal(); }
    
    LocalTerminalContainer* GetTerminalContainer() const { return m_terminalContainer.get(); }
    void SetTerminalContainer(std::unique_ptr<LocalTerminalContainer> container);
    
    TermGLCanvas* GetCanvas() const { return m_canvas; }
    void SetCanvas(TermGLCanvas* canvas);
    
    void SetSSHThread(TerminalThread* thread) { m_sshThread = thread; }
    TerminalThread* GetSSHThread() const { return m_sshThread; }
    
    void ShowContextMenu();
    bool HasTerminal() const { return m_terminalContainer != nullptr || m_sshThread != nullptr; }
    
    // Called when this panel becomes the active rendering target
    void Activate();
    // Called when this panel is no longer the active rendering target
    void Deactivate();
    
    // Public split interface
    void DoSplit(wxSplitMode mode);
    
    // Get EventProxy for canvas communication
    EventProxyPtr GetEventProxy() const { return m_eventProxy; }

    // Setup canvas connection (public for SplitManager)
    void SetupCanvasConnection();

    // File transfer request handler
    void OnFileTransferRequest(wxCommandEvent& event);

    // Input buffer for command interception
    void AppendToInputBuffer(const std::string& text);
    void ClearInputBuffer();
    const std::string& GetInputBuffer() const { return m_inputBuffer; }
    bool IsLocalTerminal() const { return m_terminalContainer != nullptr; }

    // Command interceptor for local terminals
    CommandInterceptor& GetCommandInterceptor() { return m_commandInterceptor; }

    // Convert local terminal to SSH terminal
    void ConvertToSSH(const DeviceConfig& device);

private:
    void OnClosePanel(wxCommandEvent& event);
    void OnTerminalDamage(wxThreadEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnKeyDown(wxKeyEvent& event);

    void UpdateCanvasFromTerminal();
    
    std::unique_ptr<LocalTerminalContainer> m_terminalContainer;
    TerminalThread* m_sshThread = nullptr;
    TermGLCanvas* m_canvas;
    wxStaticText* m_text;
    EventProxyPtr m_eventProxy;
    int m_prevRows = 0;
    int m_prevCols = 0;

    // Input buffer for command interception
    std::string m_inputBuffer;
    CommandInterceptor m_commandInterceptor;
};

#endif
