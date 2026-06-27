#pragma once

#include <wx/wx.h>
#include <memory>
#include <functional>

// EventProxy acts as a mediator between threads and UI windows
// Threads hold a shared_ptr to EventProxy, which holds a weak reference to the actual UI window
// This allows dynamic panel switching without reparenting
class EventProxy {
public:
    // Callback type for terminal damage events
    using DamageCallback = std::function<void(int rows, int cols, int cursor_row, int cursor_col, int first_nonempty_char)>;
    
    // Callback type for user input
    using InputCallback = std::function<void(const char* data, int length)>;

    EventProxy() = default;
    ~EventProxy() = default;

    // Set the target UI window (weak reference)
    void SetTarget(wxWindow* target) {
        m_target = target;
    }

    // Get the target UI window (returns nullptr if window was destroyed)
    wxWindow* GetTarget() const {
        return m_target;
    }

    // Set damage callback
    void SetDamageCallback(DamageCallback callback) {
        m_damageCallback = callback;
    }

    // Set input callback
    void SetInputCallback(InputCallback callback) {
        m_inputCallback = callback;
    }

    // Post damage event to UI thread (safe to call from any thread)
    void PostDamageEvent(int rows, int cols, int cursor_row, int cursor_col, int first_nonempty_char) {
        if (!m_target) return;
        
        // Check if window is being deleted
        if (m_target->IsBeingDeleted()) {
            return;
        }
        
        // Call the damage callback directly (will be called from UI thread if set up properly)
        if (m_damageCallback) {
            m_damageCallback(rows, cols, cursor_row, cursor_col, first_nonempty_char);
        }
    }

    // Send input data (safe to call from any thread)
    void SendInput(const char* data, int length) {
        if (m_inputCallback) {
            m_inputCallback(data, length);
        }
    }

private:
    wxWindow* m_target = nullptr;
    DamageCallback m_damageCallback;
    InputCallback m_inputCallback;
};

// Shared pointer type for EventProxy
using EventProxyPtr = std::shared_ptr<EventProxy>;
