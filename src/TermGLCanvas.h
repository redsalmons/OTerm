#ifndef TERMGLCANVAS_H

#define TERMGLCANVAS_H



#include <wx/wx.h>

#include <wx/glcanvas.h>

#include <wx/splitter.h>

#include <vector>

#include <map>

#include "CellInstance.h"

#include "FontAtlas.h"

#include "DeviceConfig.h"



#include "ConnectInfo.h"
#include "CommandInterceptor.h"



class TerminalPanel;

class TerminalThread;

class wxPopupTransientWindow;
class wxBookCtrlBase;



class TermGLCanvas : public wxGLCanvas {

public:

    TermGLCanvas(wxWindow* parent, bool createThread = true);

    ~TermGLCanvas();



    void UpdateScreenData(const std::vector<CellInstance>& instances);

    void SetCursorPosition(int row, int col, bool in_alt_screen = false, int vterm_scroll_offset = 0);

    void SetCursorVisible(bool visible) { m_cursor_visible = visible; }

    void ClearScreenData();

    float GetDPIScale() const { return m_dpiScale; }

    // Scale factor between logical window size and OpenGL framebuffer pixels.
    // On macOS the GL drawable uses view (point) coordinates, so this is 1.
    float GLPixelScale() const;
    float GLFramebufferScale() const;



    // Stop terminal threads (called before window destruction to avoid hanging)

    void StopThreads();

    

    // Reinitialize OpenGL context after reparent
    void ReinitializeGLContext();

    // Detach the GL context from this canvas's view (macOS) so a hidden
    // canvas's GL surface can't be composited over the visible page.
    void ReleaseGLDrawable();

    // Release GL drawables of canvases on non-selected pages and refresh the
    // selected page. Call whenever the book's selection changes.
    static void SyncBookVisibility(wxBookCtrlBase* book);



    typedef std::function<void(const char* data, int length)> KeyCallback;

    void SetKeyCallback(KeyCallback callback) { key_callback_ = callback; }

    

    typedef std::function<void(int lines)> ScrollCallback;

    void SetScrollCallback(ScrollCallback callback) { scroll_callback_ = callback; }

    

    typedef std::function<void(int row, int col, int button)> MouseCallback;

    void SetMouseCallback(MouseCallback callback) { mouse_callback_ = callback; }

    

    // Split callbacks

    typedef std::function<void(wxSplitMode mode, TerminalPanel* sourcePanel)> SplitCallback;

    void SetSplitCallback(SplitCallback callback) { split_callback_ = callback; }

    typedef std::function<void(TerminalPanel* sourcePanel)> CloseCallback;

    void SetCloseCallback(CloseCallback callback) { close_callback_ = callback; }



private:

    void OnPaint(wxPaintEvent& event);

    void OnSize(wxSizeEvent& event);

    void OnKeyDown(wxKeyEvent& event);

    void OnChar(wxKeyEvent& event);

    void OnCharHook(wxKeyEvent& event);

    void OnMouseWheel(wxMouseEvent& event);

    void OnMouseLeftDown(wxMouseEvent& event);

    void OnMouseLeftUp(wxMouseEvent& event);

    void OnMouseMove(wxMouseEvent& event);

    void OnMouseRightDown(wxMouseEvent& event);

    void OnHorizontalSplit(wxCommandEvent& event);

    void OnVerticalSplit(wxCommandEvent& event);

    void OnClosePanel(wxCommandEvent& event);

    void OnSetFocus(wxFocusEvent& event);

    void OnKillFocus(wxFocusEvent& event);

    void InitializeGL();

    bool CreateFontAtlas();

    void InitializeFontMetrics();

    void Render();

    bool HandleZoomInput(const wxKeyEvent& event);

    void BroadcastFontZoom();

    void CopySelectionToClipboard();

    

public:

    void ShowIMEInputBox();

    void HideIMEInputBox();

    void UpdateIMEBoxPlacement();

    // Rebuild the font atlas at the current GlobalConfig zoom level and refit
    // the terminal grid. Called for every canvas when the zoom changes.
    void ApplyFontZoom();



    wxGLContext* m_glContext;

    // Zoom requested while hidden; applied on next paint
    bool m_fontZoomPending = false;

    FontAtlas* m_fontAtlas;

    GLuint m_testTextureID;



    std::vector<CellInstance> m_screen_cells;

    int m_rows_count;

    int m_cols_count;

    int m_cursor_row;

    int m_cursor_col;

    bool m_cursor_visible;
    bool m_cursor_out_of_bounds;

    int m_scroll_offset;

    int m_cached_cell_height;

    KeyCallback key_callback_;

    ScrollCallback scroll_callback_;

    MouseCallback mouse_callback_;

    SplitCallback split_callback_;

    CloseCallback close_callback_;

    int m_wheelRotationAccumulator;

    int m_zoomWheelAccumulator;

    bool m_glInitialized;

    float m_dpiScale;



    // Selection state

    bool m_selecting;

    int m_selection_start_row;

    int m_selection_start_col;

    int m_selection_end_row;

    int m_selection_end_col;

    

    // Font size for cell size calculation

    int m_fontSize;

    int m_charHeight; // Actual character rendering height from FontAtlas

    int m_cellWidth;  // Cached cell width (calculated from font size)

    int m_cellHeight; // Cached cell height (calculated from char height)



    // Cursor rect (calculated once, used by both rendering and IME positioning)

    wxRect m_cursorRect;



    // Local IME input box

    wxTextCtrl* m_imeInputBox;

    // Floating window hosting the IME box on Windows. A native child HWND over
    // the GL surface cannot be composited by GDI and leaves a dead patch, so
    // it must live in a top-level popup instead.

    wxPopupTransientWindow* m_imePopup;

    bool m_imeInputBoxVisible;

    // Called when committed text arrives from the macOS IME view.
    void OnIMECommit(const std::string& utf8);

#ifdef __APPLE__

    // On macOS the focused NSTextView consumes navigation keys at the AppKit
    // level before wx can generate any key event, so they are intercepted by
    // an NSEvent local monitor instead (MacKeyMonitor.mm).
    void* m_macKeyMonitorToken;
    void EnsureMacKeyMonitor();
    bool HandleMacRawKey(unsigned short keyCode, unsigned long modifierFlags);

#endif

    std::function<void(const char*, int)> m_imeCallback;
    
    std::string m_sshInputBuffer;  // Track input for SSH terminal command detection
    CommandInterceptor m_commandInterceptor;  // Command interceptor for SSH terminal

    // IME input box event handlers

    void OnProxyTextReceived(wxCommandEvent& event);

    void OnProxyKeyDown(wxKeyEvent& event);

    void OnIMETextLostFocus(wxFocusEvent& event);



    // Terminal threads
    bool m_ownsThreads; // Track if we created and own the threads
    bool m_threadsStopped; // Track if StopThreads has been called
    DeviceConfig m_deviceConfig;



    enum {

        ID_HORIZONTAL_SPLIT = wxID_HIGHEST + 500,

        ID_VERTICAL_SPLIT,

        ID_CLOSE_PANEL

    };



};



#endif

