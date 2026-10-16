#pragma once

#ifdef __APPLE__

#include <functional>

class wxWindow;

namespace MacKeyMonitor {

// Handler receives the raw macOS virtual key code and NSEvent modifierFlags.
// Return true to consume the event so it never reaches the focused view.
using KeyHandler = std::function<bool(unsigned short keyCode, unsigned long modifierFlags)>;

// Installs a shared NSEvent local monitor (once) and registers `handler` for
// key-down events targeted at the window containing `win`. Returns a token
// for Unregister(), or nullptr if the window is not yet realized.
void* Register(wxWindow* win, KeyHandler handler);
void Unregister(void* token);

} // namespace MacKeyMonitor

#endif // __APPLE__
