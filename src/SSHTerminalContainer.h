#ifndef SSH_TERMINAL_CONTAINER_H
#define SSH_TERMINAL_CONTAINER_H

#include <wx/wx.h>
#include <wx/socket.h>
#include <wx/timer.h>
#include <libssh2.h>
#include <string>
#include <mutex>
#include <atomic>
#include <memory>
#ifndef _WIN32
#include <wx/evtloopsrc.h>
#endif
#include "ITerminalContainer.h"
#include "DeviceConfig.h"
#include "VTermManager.h"
#include "ScreenBuffer.h"

wxDECLARE_EVENT(wxEVT_SSH_DNS_RESOLVED, wxCommandEvent);

class SSHTerminalContainer : public wxEvtHandler, public ITerminalContainer
#ifndef _WIN32
                           , public wxEventLoopSourceHandler
#endif
{
public:
    enum SSHState {
        SSH_DISCONNECTED,
        SSH_CONNECTING,
        SSH_HANDSHAKING,
        SSH_PROMPTING_USERNAME,
        SSH_PROMPTING_PASSWORD,
        SSH_AUTHENTICATING,
        SSH_CHANNEL_OPENING,
        SSH_PTY_REQUESTING,
        SSH_SHELL_REQUESTING,
        SSH_READY
    };

    SSHTerminalContainer(int rows, int cols, const DeviceConfig& config);
    ~SSHTerminalContainer() override;

    // ITerminalContainer implementation overrides
    void SetUIHandler(wxWindow* ui_handler) override;
    void ClearUIHandler() override;
    void StopTerminal() override;
    bool IsSessionAlive() const override;
    void QueueInput(const std::string& input) override;
    void Resize(int rows, int cols) override;
    void Scroll(int lines) override;
    void ResetScrollToBottom() override;
    const ScreenBuffer* GetFrontBuffer() const override;
    void CopyFrontBuffer(ScreenBuffer& dest) const override;
    bool IsInAlternateScreen() const override;
    int GetScrollOffset() const override;

    // Direct SSH methods
    void Connect();
    SSHState GetState() const { return m_sshState; }
    DeviceConfig GetDeviceConfig() const { return m_deviceConfig; }

#ifndef _WIN32
    // wxEventLoopSourceHandler overrides
    void OnReadWaiting() override;
    void OnWriteWaiting() override;
    void OnExceptionWaiting() override;
#endif

private:
    // Socket and Timer Event Handlers
    void OnSocketEvent(wxSocketEvent& event);
    void OnKeepAliveTimer(wxTimerEvent& event);
    void OnConnectTimeout(wxTimerEvent& event);
    void OnDnsResolved(wxCommandEvent& event);
    void DriveStateMachine();

    // SSH Connection Step Handlers
    void ContinueHandshake();
    void StartLoginPrompt();
    void StartPasswordPrompt();
    void PerformAuthentication();
    void OpenSSHChannel();
    void RequestPTY();
    void RequestShell();
    void ProcessSSHData();
    void FlushOutbound();
    void SendStatusMessage(const std::string& msg);
    
    // Internal Utilities
    void Cleanup();
    void HandleDirections();
    void WatchInboundOnly();
    void ArmConnectTimeout();
    void ConnectSocket(const std::string& ip);
    void SwapBuffers();
    void UpdateBackBuffer();
    void TriggerDamage();
    static const char* GetStateName(SSHState state);

private:
    DeviceConfig m_deviceConfig;
    int m_rows;
    int m_cols;
    wxWindow* m_uiHandler;

    // wxSocket & SSH handles
    wxSocketClient* m_socket;
    LIBSSH2_SESSION* m_sshSession;
    LIBSSH2_CHANNEL* m_sshChannel;
    SSHState m_sshState;

    // Timers
    wxTimer m_keepAliveTimer;
    wxTimer m_connectTimer;

    // Terminal Emulator
    VTermManager m_vtermManager;
    ScreenBuffer m_frontBuffer;
    ScreenBuffer m_backBuffer;

    // Interactive state
    int m_authRetryCount;
    std::string m_pendingInput;
    bool m_hasDamage;

    // Outbound data queued when the SSH channel applies backpressure
    std::string m_outQueue;

    // Liveness sentinel for detached DNS worker threads
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);

#ifndef _WIN32
    wxEventLoopSource* m_fdSource = nullptr;
#endif

    // Socket Event Identifier
    static const int SOCKET_ID = 10001;
    static const int KEEPALIVE_TIMER_ID = 10003;
    static const int CONNECT_TIMER_ID = 10004;
    static const int CONNECT_TIMEOUT_MS = 30000;
};

#endif // SSH_TERMINAL_CONTAINER_H
