#import <Cocoa/Cocoa.h>

extern "C" void SetupMacTitleBar(void* windowHandle) {
    @try {
        // wxWidgets on macOS returns the NSView, not NSWindow
        // Get the NSWindow from the view
        NSView* view = (NSView*)windowHandle;
        NSWindow* nsWindow = [view window];
        
        if (nsWindow) {
            // Make title bar transparent
            [nsWindow setTitlebarAppearsTransparent:YES];
            [nsWindow setTitleVisibility:NSWindowTitleHidden];
            [nsWindow setTitle:@""];
            
            // Use full size content view to extend content into title bar
            NSUInteger styleMask = [nsWindow styleMask];
            styleMask |= (1UL << 15); // NSWindowStyleMaskFullSizeContentView
            [nsWindow setStyleMask:styleMask];
        }
    } @catch (NSException *exception) {
        // Ignore exceptions to prevent crash
    }
}
