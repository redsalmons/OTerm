#include "MacIMEProxy.h"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#include <wx/wx.h>
#include <objc/runtime.h>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <string>
#include <cstdlib>
#include "SSHManager.h"

// Input context that declines to handle command/terminal keys. TSM
// pre-dispatches keyDown events through [ctx handleEvent:] — returning NO
// for these lets them reach menu shortcuts (Cmd+V), Ctrl combos and the
// terminal's navigation keys instead of being swallowed by the IM session.
@interface OTermInputContext : NSTextInputContext
@end

namespace {

struct IMEState {
    MacIME::Handlers handlers;
    NSTextInputContext* ctx = nil;
    bool hasMarked = false;
    unsigned long markedLen = 0;
};

// canvas view -> state
std::unordered_map<NSView*, IMEState> g_states;
std::unordered_set<Class> g_patchedClasses;
std::recursive_mutex g_mutex;

bool KeyDebugOn() {
    static const bool on = getenv("OTERM_KEY_DEBUG") != nullptr;
    return on;
}

IMEState* StateFor(id self) {
    auto it = g_states.find((NSView*)self);
    return it == g_states.end() ? nullptr : &it->second;
}

NSString* PlainString(id aString) {
    if ([aString isKindOfClass:[NSAttributedString class]]) {
        return [(NSAttributedString*)aString string];
    }
    return (NSString*)aString;
}

bool IsTerminalKeyCode(unsigned short kc) {
    switch (kc) {
        case 36: case 76:                       // Return, KP Enter
        case 48: case 51: case 53:              // Tab, Backspace, Esc
        case 114: case 117:                     // Insert/Help, FwdDel
        case 115: case 116: case 119: case 121: // Home, PgUp, End, PgDn
        case 123: case 124: case 125: case 126: // arrows
        case 122: case 120: case 99:  case 118: // F1-F4
        case 96:  case 97:  case 98:  case 100: // F5-F8
        case 101: case 109: case 103: case 111: // F9-F12
        case 105: case 107: case 113: case 106: // F13-F16
        case 64:  case 79:  case 80:  case 90:  // F17-F20
            return true;
        default:
            return false;
    }
}

// Adds `sel` backed by `block` to `cls`, using the NSTextInputClient protocol's
// own type encoding.
void AddClientMethod(Class cls, SEL sel, id block, bool required) {
    Protocol* p = @protocol(NSTextInputClient);
    struct objc_method_description d = protocol_getMethodDescription(p, sel, required, YES);
    const char* types = d.types ? d.types : "v@:";
    if (!class_addMethod(cls, sel, imp_implementationWithBlock(block), types)) {
        // Already implemented by the class — leave it alone.
    }
}

void PatchCanvasClass(Class cls) {
    // --- NSTextInputClient -------------------------------------------------

    AddClientMethod(cls, @selector(insertText:replacementRange:),
        ^(id self, id aString, NSRange replacementRange) {
            IMEState* st = StateFor(self);
            if (!st) return;
            NSString* s = PlainString(aString);
            st->hasMarked = false;
            st->markedLen = 0;
            if (s && st->handlers.commit) {
                const char* utf8 = [s UTF8String];
                st->handlers.commit(utf8 ? std::string(utf8) : std::string());
            }
        }, YES);

    AddClientMethod(cls, @selector(setMarkedText:selectedRange:replacementRange:),
        ^(id self, id aString, NSRange selectedRange, NSRange replacementRange) {
            IMEState* st = StateFor(self);
            if (!st) return;
            NSString* s = PlainString(aString);
            st->markedLen = s ? (unsigned long)[s length] : 0;
            st->hasMarked = st->markedLen > 0;
        }, YES);

    AddClientMethod(cls, @selector(unmarkText),
        ^(id self) {
            IMEState* st = StateFor(self);
            if (st) { st->hasMarked = false; st->markedLen = 0; }
        }, YES);

    AddClientMethod(cls, @selector(hasMarkedText),
        ^BOOL(id self) {
            IMEState* st = StateFor(self);
            return st && st->hasMarked ? YES : NO;
        }, YES);

    AddClientMethod(cls, @selector(markedRange),
        ^NSRange(id self) {
            IMEState* st = StateFor(self);
            if (st && st->hasMarked) return NSMakeRange(0, st->markedLen);
            return NSMakeRange(NSNotFound, 0);
        }, YES);

    AddClientMethod(cls, @selector(selectedRange),
        ^NSRange(id self) {
            IMEState* st = StateFor(self);
            if (st && st->hasMarked) return NSMakeRange(st->markedLen, 0);
            return NSMakeRange(0, 0);
        }, YES);

    AddClientMethod(cls, @selector(validAttributesForMarkedText),
        ^NSArray*(id self) {
            return @[];
        }, YES);

    AddClientMethod(cls, @selector(attributedSubstringForProposedRange:actualRange:),
        ^NSAttributedString*(id self, NSRange range, NSRangePointer actualRange) {
            return (NSAttributedString*)nil;
        }, YES);

    AddClientMethod(cls, @selector(firstRectForCharacterRange:actualRange:),
        ^NSRect(id self, NSRange range, NSRangePointer actualRange) {
            NSView* view = (NSView*)self;
            NSRect vr = NSMakeRect(0, 0, 0, 0);
            IMEState* st = StateFor(self);
            if (st && st->handlers.cursorRect) {
                double x = 0, y = 0, w = 0, h = 0;
                st->handlers.cursorRect(x, y, w, h);
                vr = NSMakeRect(x, y, w, h);
            }
            if (![view isFlipped]) {
                vr.origin.y = [view bounds].size.height - vr.origin.y - vr.size.height;
            }
            NSRect wr = [view convertRect:vr toView:nil];
            return [[view window] convertRectToScreen:wr];
        }, YES);

    AddClientMethod(cls, @selector(characterIndexForPoint:),
        ^NSUInteger(id self, NSPoint point) {
            return (NSUInteger)0;
        }, YES);

    // Commands the input context asks the client to perform (candidate
    // navigation, cancel, etc.). The terminal owns command keys; swallow.
    if (!class_getInstanceMethod(cls, @selector(doCommandBySelector:))) {
        class_addMethod(cls, @selector(doCommandBySelector:),
            imp_implementationWithBlock(^(id self, SEL selector) {}), "v@::");
    }

    // Stop unhandled key events from bubbling up the responder chain.
    // Without this, NSWindow plays the system beep ("click") once per
    // keystroke the canvas doesn't consume.
    class_addMethod(cls, @selector(noResponderFor:),
        imp_implementationWithBlock(^(id self, SEL selector) {
            if (KeyDebugOn()) {
                SSH_LOG("IME_DEBUG canvas noResponderFor: " << sel_getName(selector));
            }
        }), "v@::");

    // inputContext override: always report a context. Navigation/command keys
    // never reach it — the keyDown: patch sends them to the original handler
    // first — while text keys are consumed by handleEvent:, which also stops
    // the per-keystroke beep AppKit plays for a text client without a context.
    {
        SEL sel = @selector(inputContext);
        IMP impl = imp_implementationWithBlock(^NSTextInputContext*(id self) {
            IMEState* st = StateFor(self);
            if (!st) return (NSTextInputContext*)nil;

            if (!st->ctx) {
                st->ctx = [[OTermInputContext alloc] initWithClient:(NSView<NSTextInputClient>*)self];
            }
            return st->ctx;
        });
        Method m = class_getInstanceMethod(cls, sel);
        if (m) method_setImplementation(m, impl);
        else class_addMethod(cls, sel, impl, "@@:");
    }

    class_addProtocol(cls, @protocol(NSTextInputClient));

    // --- keyDown ------------------------------------------------------------
    //
    // Not composing: terminal/navigation/modified keys go straight to the
    // original (wx) handler; only keys that can produce text are offered to
    // the input context. While composing, everything goes to the IM first so
    // candidate navigation works. Note inputContext itself returns nil while
    // an ASCII-capable source is selected, which keeps TSM from pre-routing
    // navigation keys to the IM session.
    {
        SEL sel = @selector(keyDown:);
        Method m = class_getInstanceMethod(cls, sel);
        if (m) {
            IMP orig = method_getImplementation(m);
            IMP patched = imp_implementationWithBlock(^(id self, NSEvent* event) {
                IMEState* st = StateFor(self);
                const unsigned short kc = event.keyCode;
                const bool dbg = KeyDebugOn();

                // hasMarked can go stale (source switched, IM ended the
                // session without unmark). Verify the input source can
                // actually compose before trusting it.
                bool composing = st && st->hasMarked;
                if (composing) {
                    bool asciiCapable = true;
                    if (TISInputSourceRef src = TISCopyCurrentKeyboardInputSource()) {
                        if (CFBooleanRef b = (CFBooleanRef)TISGetInputSourceProperty(
                                src, kTISPropertyInputSourceIsASCIICapable)) {
                            asciiCapable = CFBooleanGetValue(b);
                        }
                        CFRelease(src);
                    }
                    if (asciiCapable) {
                        st->hasMarked = false;
                        composing = false;
                    }
                }

                if (!composing) {
                    const NSEventModifierFlags mods =
                        event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
                    const bool cmdOrCtrl =
                        (mods & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0;

                    if (IsTerminalKeyCode(kc) || cmdOrCtrl) {
                        if (dbg) SSH_LOG("IME_DEBUG keyDown kc=" << kc << " -> orig (terminal/cmd key)");
                        ((void(*)(id, SEL, NSEvent*))orig)(self, @selector(keyDown:), event);
                        return;
                    }
                }

                NSTextInputContext* ctx = [self inputContext];
                if (ctx && [ctx handleEvent:event]) {
                    if (dbg) SSH_LOG("IME_DEBUG keyDown kc=" << kc << " -> handled by inputContext");
                    return;
                }
                // IM declined a key while we thought we were composing —
                // clear stale marked state so subsequent keys flow normally.
                if (st && st->hasMarked) st->hasMarked = false;
                if (dbg) SSH_LOG("IME_DEBUG keyDown kc=" << kc << " -> orig (ctx=" << (ctx ? "yes" : "nil") << ")");
                ((void(*)(id, SEL, NSEvent*))orig)(self, @selector(keyDown:), event);
            });
            method_setImplementation(m, patched);
        }
    }
}

} // namespace

@implementation OTermInputContext
- (BOOL)handleEvent:(NSEvent*)event {
    if ([event type] != NSEventTypeKeyDown) {
        return [super handleEvent:event];
    }

    // Menu shortcuts and Ctrl combos always bypass the IM path.
    const NSEventModifierFlags mods =
        event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
    if (mods & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) {
        return NO;
    }

    bool asciiCapable = true;
    if (TISInputSourceRef src = TISCopyCurrentKeyboardInputSource()) {
        if (CFBooleanRef b = (CFBooleanRef)TISGetInputSourceProperty(
                src, kTISPropertyInputSourceIsASCIICapable)) {
            asciiCapable = CFBooleanGetValue(b);
        }
        CFRelease(src);
    }
    if (!asciiCapable) {
        // A real input method is active — let TSM route (composition,
        // candidate window, commit via insertText:).
        return [super handleEvent:event];
    }

    // ASCII source: there is no IM to route through, and interpretKeyEvents:
    // stops at our context (returning NO produces nothing, which is why
    // reporting a plain NSTextInputContext killed ASCII input). Deliver the
    // text ourselves — insertText: on the client generates wxEVT_CHAR.
    // Keys that produce no characters (arrows, F-keys, nav keys) are declined
    // so they fall back to the original keyDown path.
    if (IsTerminalKeyCode(event.keyCode)) return NO;
    NSString* chars = [event characters];
    if (chars.length == 0) return NO;
    unichar c = [chars characterAtIndex:0];
    if (c < 0x20 || (c >= 0xE000 && c <= 0xF8FF) || c == 0x7F) return NO;
    [(NSView<NSTextInputClient>*)self.client
        insertText:chars replacementRange:NSMakeRange(NSNotFound, 0)];
    return YES;
}
@end

// Belt and braces: silence the "unhandled key" beep app-wide for keyDowns
// that end up at a window (e.g. when the first responder is not the
// canvas). Only keyDown: is swallowed; other selectors forward as usual.
static void SilenceWindowKeyBeep() {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        Method m = class_getInstanceMethod([NSWindow class], @selector(noResponderFor:));
        if (!m) return;
        IMP orig = method_getImplementation(m);
        IMP patched = imp_implementationWithBlock(^(id self, SEL selector) {
            if (selector == @selector(keyDown:)) {
                if (KeyDebugOn()) {
                    SSH_LOG("IME_DEBUG window noResponderFor keyDown (swallowed beep)");
                }
                return;
            }
            ((void(*)(id, SEL, SEL))orig)(self, @selector(noResponderFor:), selector);
        });
        method_setImplementation(m, patched);
    });
}

void MacIME::Enable(wxWindow* canvas, const Handlers& handlers) {
    if (!canvas) return;
    NSView* view = (NSView*)canvas->GetHandle();
    if (!view) return;

    SilenceWindowKeyBeep();

    std::lock_guard<std::recursive_mutex> lock(g_mutex);

    auto it = g_states.find(view);
    if (it == g_states.end()) {
        g_states[view] = IMEState{handlers, nil, false, 0};
    } else {
        it->second.handlers = handlers;
    }

    Class cls = [view class];
    if (g_patchedClasses.count(cls)) return;
    PatchCanvasClass(cls);
    g_patchedClasses.insert(cls);
}

void MacIME::Disable(wxWindow* canvas) {
    if (!canvas) return;
    NSView* view = (NSView*)canvas->GetHandle();
    if (!view) return;
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_states.erase(view);
}

#endif // __APPLE__
