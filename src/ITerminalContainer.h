#ifndef I_TERMINAL_CONTAINER_H
#define I_TERMINAL_CONTAINER_H

#include <wx/wx.h>
#include <string>
#include "ScreenBuffer.h"

// Forward declaration of EventProxy
#include "EventProxy.h"

class ITerminalContainer {
public:
    virtual ~ITerminalContainer() = default;

    // Set UI event handler (typically TermGLCanvas) to receive refresh events
    virtual void SetUIHandler(wxWindow* ui_handler) = 0;
    virtual void ClearUIHandler() = 0;
    
    // Stop the terminal session and release all associated resources
    virtual void StopTerminal() = 0;
    
    // Check if the underlying connection/session is alive
    virtual bool IsSessionAlive() const = 0;

    // Enqueue user input (keystrokes or pasted text) to send to terminal
    virtual void QueueInput(const std::string& input) = 0;
    
    // Resize the virtual terminal dimensions (rows, columns)
    virtual void Resize(int rows, int cols) = 0;
    
    // Scroll the history/backbuffer by some lines (positive for up, negative for down)
    virtual void Scroll(int lines) = 0;
    
    // Reset any scroll history back to the live bottom view
    virtual void ResetScrollToBottom() = 0;

    // Get read-only pointer to front buffer (thread-safe/safe for UI thread reading)
    virtual const ScreenBuffer* GetFrontBuffer() const = 0;
    
    // Copy the front buffer data safely to local destination
    virtual void CopyFrontBuffer(ScreenBuffer& dest) const = 0;
    
    // Check if the shell/remote is currently in alternate screen mode (e.g. running vi, top)
    virtual bool IsInAlternateScreen() const = 0;
    
    // Get the current scrollback scroll offset
    virtual int GetScrollOffset() const = 0;
};

#endif // I_TERMINAL_CONTAINER_H
