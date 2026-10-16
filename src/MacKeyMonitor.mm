#include "MacKeyMonitor.h"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include <wx/wx.h>
#include <vector>
#include <memory>
#include <mutex>

namespace {

struct KeyMonitorEntry {
    void* window;
    MacKeyMonitor::KeyHandler handler;
    bool dead = false;
};

std::vector<std::shared_ptr<KeyMonitorEntry>> g_entries;
std::recursive_mutex g_mutex;
id g_monitor = nil;

void EnsureMonitorInstalled() {
    if (g_monitor) return;
    g_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
        handler:^NSEvent*(NSEvent* event) {
            std::lock_guard<std::recursive_mutex> lock(g_mutex);
            for (auto& entry : g_entries) {
                if (entry->dead) continue;
                if ((void*)[event window] != entry->window) continue;
                if (entry->handler(event.keyCode, (unsigned long)event.modifierFlags)) {
                    return (NSEvent*)nil;
                }
            }
            return event;
        }];
}

} // namespace

void* MacKeyMonitor::Register(wxWindow* win, KeyHandler handler) {
    if (!win) return nullptr;
    NSView* view = (NSView*)win->GetHandle();
    NSWindow* window = view ? [view window] : nil;
    if (!window) return nullptr;

    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    EnsureMonitorInstalled();
    auto entry = std::make_shared<KeyMonitorEntry>();
    entry->window = (void*)window;
    entry->handler = std::move(handler);
    g_entries.push_back(entry);
    return entry.get();
}

void MacKeyMonitor::Unregister(void* token) {
    if (!token) return;
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    auto* entry = static_cast<KeyMonitorEntry*>(token);
    entry->dead = true;
    for (auto it = g_entries.begin(); it != g_entries.end(); ++it) {
        if (it->get() == entry) { g_entries.erase(it); break; }
    }
}

#endif // __APPLE__
