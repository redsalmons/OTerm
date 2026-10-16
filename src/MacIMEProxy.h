#pragma once

#ifdef __APPLE__

#include <functional>
#include <string>

class wxWindow;

namespace MacIME {

struct Handlers {
    // Called with the committed UTF-8 text from insertText:.
    std::function<void(const std::string&)> commit;
    // Called to provide the cursor rectangle in view (point) coordinates,
    // used to position the IME candidate window.
    std::function<void(double& x, double& y, double& w, double& h)> cursorRect;
};

// Makes the given canvas's native NSView a first-class NSTextInputClient and
// routes keyDown through the input context first. The canvas keeps focus, so
// all keys (including arrows) flow through the normal wx key path unless an
// input method is actively composing.
void Enable(wxWindow* canvas, const Handlers& handlers);
void Disable(wxWindow* canvas);

} // namespace MacIME

#endif // __APPLE__
