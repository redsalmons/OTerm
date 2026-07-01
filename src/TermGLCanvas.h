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



class LocalTerminalThread;

class TerminalPanel;

class TerminalThread;



class TermGLCanvas : public wxGLCanvas {

public:

    TermGLCanvas(wxWindow* parent, bool createThread = true);

    ~TermGLCanvas();



    void UpdateScreenData(const std::vector<CellInstance>& instances);

    void SetCursorPosition(int row, int col, bool in_alt_screen = false, int vterm_scroll_offset = 0);

    void SetCursorVisible(bool visible) { m_cursor_visible = visible; }

    void ClearScreenData();

    float GetDPIScale() const { return m_dpiScale; }



    // Convert local terminal to SSH terminal

    void ConvertToSSH(const std::string& username, const std::string& address, int port = 22);

    void ConvertToSSH(const DeviceConfig& device);



    // Stop terminal threads (called before window destruction to avoid hanging)

    void StopThreads();

    

    // Reinitialize OpenGL context after reparent
    void ReinitializeGLContext();
    
    // Get local terminal thread

    

    // Get local terminal thread

    LocalTerminalThread* GetLocalTerminalThread() const { return m_localTerminalThread; }



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

    void InitializeFontMetrics();

    void Render();

    void CopySelectionToClipboard();

    

public:

    void ShowIMEInputBox();

    void HideIMEInputBox();



    wxGLContext* m_glContext;

    FontAtlas* m_fontAtlas;

    GLuint m_testTextureID;



    std::vector<CellInstance> m_screen_cells;

    int m_rows_count;

    int m_cols_count;

    int m_cursor_row;

    int m_cursor_col;

    bool m_cursor_visible;

    int m_scroll_offset;

    int m_cached_cell_height;

    KeyCallback key_callback_;

    ScrollCallback scroll_callback_;

    MouseCallback mouse_callback_;

    SplitCallback split_callback_;

    CloseCallback close_callback_;

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

    bool m_imeInputBoxVisible;

    std::function<void(const char*, int)> m_imeCallback;



    // IME input box event handlers

    void OnProxyTextReceived(wxCommandEvent& event);

    void OnProxyKeyDown(wxKeyEvent& event);

    void OnIMETextLostFocus(wxFocusEvent& event);



    // Terminal threads
    LocalTerminalThread* m_localTerminalThread;
    TerminalThread* m_terminalThread;
    bool m_ownsThreads; // Track if we created and own the threads
    DeviceConfig m_deviceConfig;



    enum {

        ID_HORIZONTAL_SPLIT = wxID_HIGHEST + 500,

        ID_VERTICAL_SPLIT,

        ID_CLOSE_PANEL

    };



};



#endif

