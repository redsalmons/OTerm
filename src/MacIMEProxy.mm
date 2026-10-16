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

    // inputContext override: report a context only while a non-ASCII-capable
    // input source (i.e. an actual input method) is selected. With an ASCII
    // keyboard layout selected we return nil — TSM then skips the pre-dispatch
    // routing that would otherwise hand raw key events to the input method
    // before sendEvent, keeping navigation keys safe.
    {
        SEL sel = @selector(inputContext);
        IMP impl = imp_implementationWithBlock(^NSTextInputContext*(id self) {
            IMEState* st = StateFor(self);
            if (!st) return (NSTextInputContext*)nil;

            bool asciiCapable = true;
            if (TISInputSourceRef src = TISCopyCurrentKeyboardInputSource()) {
                if (CFBooleanRef b = (CFBooleanRef)TISGetInputSourceProperty(
                        src, kTISPropertyInputSourceIsASCIICapable)) {
                    asciiCapable = CFBooleanGetValue(b);
                }
                CFRelease(src);
            }
            if (asciiCapable) return (NSTextInputContext*)nil;

            if (!st->ctx) {
                st->ctx = [[NSTextInputContext alloc] initWithClient:(NSView<NSTextInputClient>*)self];
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
                const bool composing = st && st->hasMarked;
                const unsigned short kc = event.keyCode;

                if (!composing) {
                    const NSEventModifierFlags mods =
                        event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
                    const bool cmdOrCtrl =
                        (mods & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0;

                    if (IsTerminalKeyCode(kc) || cmdOrCtrl) {
                        ((void(*)(id, SEL, NSEvent*))orig)(self, @selector(keyDown:), event);
                        return;
                    }
                }

                NSTextInputContext* ctx = [self inputContext];
                if (ctx && [ctx handleEvent:event]) return;
                ((void(*)(id, SEL, NSEvent*))orig)(self, @selector(keyDown:), event);
            });
            method_setImplementation(m, patched);
        }
    }
}

} // namespace

void MacIME::Enable(wxWindow* canvas, const Handlers& handlers) {
    if (!canvas) return;
    NSView* view = (NSView*)canvas->GetHandle();
    if (!view) return;

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
