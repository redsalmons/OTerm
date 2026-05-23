#ifndef TERMGLCANVAS_H
#define TERMGLCANVAS_H

#include <wx/wx.h>
#include <wx/glcanvas.h>
#include <vector>
#include <map>
#include "CellInstance.h"
#include "FontAtlas.h"

#include "ConnectInfo.h"

class TermGLCanvas : public wxGLCanvas {
public:
    TermGLCanvas(wxWindow* parent);
    ~TermGLCanvas();

    void UpdateScreenData(const std::vector<CellInstance>& instances);
    void SetCursorPosition(int row, int col);
    void SetCursorVisible(bool visible) { m_cursor_visible = visible; }
    void ClearScreenData();

    typedef std::function<void(const char* data, int length)> KeyCallback;
    void SetKeyCallback(KeyCallback callback) { key_callback_ = callback; }
    
    typedef std::function<void(int lines)> ScrollCallback;
    void SetScrollCallback(ScrollCallback callback) { scroll_callback_ = callback; }
    

private:
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnChar(wxKeyEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnMouseLeftDown(wxMouseEvent& event);
    void OnMouseLeftUp(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseRightDown(wxMouseEvent& event);
    void InitializeGL();
    void Render();
    void CopySelectionToClipboard();

    wxGLContext* m_glContext;
    FontAtlas* m_fontAtlas;
    GLuint m_testTextureID;

    std::map<std::pair<int, int>, CellInstance> m_screen_cells;
    int m_cursor_row;
    int m_cursor_col;
    bool m_cursor_visible;
    int m_scroll_offset;
    int m_cached_cell_height;
    KeyCallback key_callback_;
    ScrollCallback scroll_callback_;
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

};

#endif
