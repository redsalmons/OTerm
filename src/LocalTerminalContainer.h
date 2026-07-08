#ifndef LOCAL_TERMINAL_CONTAINER_H
#define LOCAL_TERMINAL_CONTAINER_H

#include <wx/wx.h>
#include <wx/timer.h>
#include <mutex>
#include "ITerminalContainer.h"
#include "LocalTerminalManager.h"
#include "VTermManager.h"
#include "ScreenBuffer.h"
#include "EventProxy.h"

class LocalTerminalContainer : public wxEvtHandler, public ITerminalContainer {
public:
    LocalTerminalContainer(int rows = 24, int cols = 80, const std::string& shell = "");
    ~LocalTerminalContainer() override;

    EventProxyPtr GetEventProxy() const { return m_event_proxy; }

    // ITerminalContainer implementation overrides
    const ScreenBuffer* GetFrontBuffer() const override;
    void CopyFrontBuffer(ScreenBuffer& dest) const override;
    void SetUIHandler(wxWindow* ui_handler) override;
    void ClearUIHandler() override;
    void StopTerminal() override;
    void QueueInput(const std::string& input) override;
    void Resize(int rows, int cols) override;
    void Scroll(int lines) override;
    void ResetScrollToBottom() override;
    bool IsSessionAlive() const override;
    bool IsInAlternateScreen() const override;
    int GetScrollOffset() const override;

private:
    void OnReadTimer(wxTimerEvent& event);
    void UpdateBackBuffer();
    void SwapBuffers();
    void TriggerDamage();

private:
    LocalTerminalManager m_terminalManager;
    VTermManager m_vtermManager;
    
    // UI communication
    EventProxyPtr m_event_proxy;
    
    // Buffers for rendering
    ScreenBuffer m_frontBuffer;
    ScreenBuffer m_backBuffer;
    mutable std::mutex m_bufferMutex;

    // Dimensions and State
    int m_rows;
    int m_cols;
    bool m_hasDamage;

    // Poll timer
    wxTimer m_readTimer;

    // Timer Event Identifier
    static const int READ_TIMER_ID = 20001;
};

#endif // LOCAL_TERMINAL_CONTAINER_H
