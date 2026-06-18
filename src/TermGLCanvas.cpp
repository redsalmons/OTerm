#include "TermGLCanvas.h"
#include <wx/wx.h>
#include <wx/display.h>
#include <wx/clipbrd.h>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include "SSHManager.h"
#include "GlobalConfig.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

TermGLCanvas::TermGLCanvas(wxWindow* parent)
    : wxGLCanvas(parent, wxID_ANY, nullptr, wxDefaultPosition, wxDefaultSize,
                 wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS | wxCLIP_CHILDREN | wxBORDER_NONE),
      m_glContext(nullptr),
      m_fontAtlas(nullptr),
      m_testTextureID(0),
      m_rows_count(0), m_cols_count(0),
      m_cursor_row(0), m_cursor_col(0), m_cursor_visible(true), m_scroll_offset(0),
      m_cached_cell_height(24), m_glInitialized(false), m_dpiScale(1.0f),
      m_selecting(false), m_selection_start_row(0), m_selection_start_col(0),
      m_selection_end_row(0), m_selection_end_col(0),
      m_fontSize(0),
      m_charHeight(0),
      m_cellWidth(0),
      m_cellHeight(0),
      m_cursorRect(0, 0, 0, 0),
      m_imeInputBox(nullptr),
      m_imeInputBoxVisible(false) {
    // Calculate DPI scale
    if (GetHandle()) {
        m_dpiScale = GetDPIScaleFactor();
    } else {
        int screenNum = wxDisplay::GetFromWindow(this);
        if (screenNum != wxNOT_FOUND) {
            wxDisplay display(screenNum);
            int dpi = display.GetPPI().GetWidth();
            m_dpiScale = static_cast<float>(dpi) / 96.0f;
        }
    }
    if (m_dpiScale <= 0.0f) m_dpiScale = 1.0f;
    
    Bind(wxEVT_PAINT, &TermGLCanvas::OnPaint, this);
    Bind(wxEVT_SIZE, &TermGLCanvas::OnSize, this);
    Bind(wxEVT_KEY_DOWN, &TermGLCanvas::OnKeyDown, this);
    Bind(wxEVT_CHAR, &TermGLCanvas::OnChar, this);
    Bind(wxEVT_CHAR_HOOK, &TermGLCanvas::OnCharHook, this);
    Bind(wxEVT_MOUSEWHEEL, &TermGLCanvas::OnMouseWheel, this);
    Bind(wxEVT_LEFT_DOWN, &TermGLCanvas::OnMouseLeftDown, this);
    Bind(wxEVT_LEFT_UP, &TermGLCanvas::OnMouseLeftUp, this);
    Bind(wxEVT_MOTION, &TermGLCanvas::OnMouseMove, this);
    Bind(wxEVT_RIGHT_DOWN, &TermGLCanvas::OnMouseRightDown, this);
    Bind(wxEVT_SET_FOCUS, &TermGLCanvas::OnSetFocus, this);
    Bind(wxEVT_KILL_FOCUS, &TermGLCanvas::OnKillFocus, this);

    // Create invisible proxy input box (12x18 pixels)
    // wxTE_RICH2 uses Rich Edit 2.0 which has better borderless support on Windows
    m_imeInputBox = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                                   wxDefaultPosition, wxSize(12, 18),
                                   wxBORDER_NONE | wxNO_BORDER | wxTE_PROCESS_ENTER | wxTE_NOHIDESEL | wxWANTS_CHARS | wxTE_RICH2);
    // Set background to blue to match cursor color
    wxColour cursor_color = wxColour(0, 120, 215); // Blue cursor color
    m_imeInputBox->SetBackgroundColour(cursor_color);
    m_imeInputBox->SetForegroundColour(cursor_color); // Text color same as background
    m_imeInputBox->Hide(); // Keep it hidden by default
    
    // Remove internal margins so the control edge matches the cursor exactly
    m_imeInputBox->SetMargins(0);
    
    // Additional styling to hide border completely
    m_imeInputBox->SetWindowStyleFlag(wxBORDER_NONE | wxNO_BORDER | wxTE_PROCESS_ENTER | wxTE_NOHIDESEL | wxWANTS_CHARS | wxTE_RICH2);
    
    // Bind IME input box events
    m_imeInputBox->Bind(wxEVT_TEXT, &TermGLCanvas::OnProxyTextReceived, this);
    m_imeInputBox->Bind(wxEVT_KEY_DOWN, &TermGLCanvas::OnProxyKeyDown, this);
    m_imeInputBox->Bind(wxEVT_KILL_FOCUS, &TermGLCanvas::OnIMETextLostFocus, this);

    SSH_LOG("TermGLCanvas constructed, IsShown: " << IsShown() << ", IsEnabled: " << IsEnabled() << ", DPI scale: " << m_dpiScale);
}

TermGLCanvas::~TermGLCanvas() {
    delete m_fontAtlas;
    delete m_glContext;
    if (m_imeInputBox) {
        delete m_imeInputBox;
        m_imeInputBox = nullptr;
    }
}

void TermGLCanvas::InitializeGL() {
    if (m_glInitialized) return;
    
    // Create OpenGL context
    m_glContext = new wxGLContext(this);
    m_glContext->SetCurrent(*this);
    
    // Set up OpenGL state (legacy OpenGL)
    glClearColor(0.04f, 0.04f, 0.04f, 1.0f); // Dark background
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    
    // Create font atlas with configured font and size
    m_fontAtlas = new FontAtlas();
    std::string fontName = GlobalConfig::GetFontName();
    int fontSize = GlobalConfig::GetFontSize();

    // Apply x2 scaling if DPI scaling is detected (high DPI screen)
    int terminalFontSize = fontSize;
    if (m_dpiScale > 1.0f) {
        terminalFontSize = static_cast<int>(fontSize * 2);
    }
    if (terminalFontSize < 8) terminalFontSize = 8;
    if (terminalFontSize > 72) terminalFontSize = 72;

    m_fontSize = terminalFontSize;
    
    if (!m_fontAtlas->InitializeSystemFont(terminalFontSize, wxString::FromUTF8(fontName.c_str()))) {
        SSH_LOG("Failed to initialize font atlas with configured font");
        return;
    }
    
    // Get actual character rendering height from font atlas
    m_charHeight = m_fontAtlas->GetCharHeight();
    if (m_charHeight == 0) {
        m_charHeight = terminalFontSize; // Fallback to font size
    }
    
    SSH_LOG("Font atlas initialized: texture ID=" << m_fontAtlas->GetTextureID()
            << ", size=" << m_fontAtlas->GetTextureWidth() << "x" << m_fontAtlas->GetTextureHeight()
            << ", charHeight=" << m_charHeight);
    
    // Initialize font metrics (cell width/height calculations)
    InitializeFontMetrics();
    
    // Post a size event to parent ConnectInfo to force recalculating VTerm rows/cols with actual cell dimensions
    wxWindow* parent = GetParent();
    if (parent) {
        wxSizeEvent sizeEvent(parent->GetSize(), parent->GetId());
        wxPostEvent(parent, sizeEvent);
    }
    
    // DEBUG: Create a simple 2x2 test texture (red, green, blue, yellow)
    GLuint testTex;
    glGenTextures(1, &testTex);
    glBindTexture(GL_TEXTURE_2D, testTex);
    unsigned char testData[16] = {
        255, 0, 0, 255,    // red
        0, 255, 0, 255,    // green
        0, 0, 255, 255,    // blue
        255, 255, 0, 255   // yellow
    };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, testData);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    SSH_LOG("Test texture created: ID=" << testTex);
    m_testTextureID = testTex;
    
    m_glInitialized = true;
    SSH_LOG("OpenGL initialized successfully");
}

void TermGLCanvas::InitializeFontMetrics() {
    // Use actual character width from font atlas instead of fixed fontSize/2
    CharMetrics wMetrics = m_fontAtlas->GetCharMetrics('W');
    m_cellWidth = static_cast<int>(wMetrics.advance);

    // Fallback if atlas metrics not yet available
    if (m_cellWidth <= 0) {
        m_cellWidth = m_fontSize / 2;
    }
    if (m_cellWidth < 6) m_cellWidth = 6;

    m_cellHeight = m_charHeight;

    // Update cached cell height for scroll calculations
    m_cached_cell_height = m_cellHeight;

    SSH_LOG("Font metrics initialized: cellWidth=" << m_cellWidth << ", cellHeight=" << m_cellHeight);
}

void TermGLCanvas::UpdateScreenData(const std::vector<CellInstance>& instances) {
    m_screen_cells = instances;
    m_rows_count = 0;
    m_cols_count = 0;
    for (const auto& cell : m_screen_cells) {
        if ((int)cell.cell_y + 1 > m_rows_count) m_rows_count = (int)cell.cell_y + 1;
        if ((int)cell.cell_x + 1 > m_cols_count) m_cols_count = (int)cell.cell_x + 1;
    }
    
    // Trigger refresh
    Refresh();
}

void TermGLCanvas::ClearScreenData() {
    m_screen_cells.clear();
    m_rows_count = 0;
    m_cols_count = 0;
}

void TermGLCanvas::SetCursorPosition(int row, int col, bool in_alt_screen, int vterm_scroll_offset) {
    m_cursor_row = row;
    m_cursor_col = col;

    // 1. Calculate and update scroll offset first, as it affects coordinate calculation
    if (in_alt_screen) {
        m_scroll_offset = 0;
    } else if (vterm_scroll_offset > 0) {
        m_scroll_offset = 0;
    } else {
        // Calculate scroll offset to keep cursor visible (account for 4px top/bottom margins, DPI-scaled)
        wxSize size = GetSize();
        int scroll_margin_y = static_cast<int>(4 * m_dpiScale);
        int availableHeight = size.GetHeight() - scroll_margin_y * 2;
        int visible_rows = availableHeight / m_cached_cell_height;
        
        // Calculate scroll offset to keep cursor at bottom
        if (m_cursor_row >= visible_rows) {
            m_scroll_offset = m_cursor_row - visible_rows + 1;
        } else {
            m_scroll_offset = 0;
        }
    }

    // 2. Now calculate cursor rect with the correct updated m_scroll_offset
    float margin_x = 8.0f * m_dpiScale;
    float margin_y = 4.0f * m_dpiScale;
    float cell_width = static_cast<float>(m_cellWidth);
    float cell_height = static_cast<float>(m_cellHeight);

    float cursor_x = col * cell_width + margin_x;
    float cursor_y = (row - m_scroll_offset) * cell_height + margin_y;
    float cursor_height = cell_height * 0.85f;
    float cursor_y_offset = (cell_height - cursor_height) / 2.0f;
    cursor_y += cursor_y_offset;

    // Save to member for Render() to use
    m_cursorRect = wxRect(static_cast<int>(cursor_x), static_cast<int>(cursor_y),
                          static_cast<int>(cell_width), static_cast<int>(cursor_height));

    // 3. Update IME input box position and size if visible
    if (m_imeInputBox && m_imeInputBoxVisible) {
        // Bounds check (same logic as ShowIMEInputBox)
        wxSize size = GetSize();
        wxRect imeRect = m_cursorRect;
        if (imeRect.x < 0) imeRect.x = 0;
        if (imeRect.y < 0) imeRect.y = 0;
        if (imeRect.x + imeRect.width > size.GetWidth()) imeRect.x = size.GetWidth() - imeRect.width;
        if (imeRect.y + imeRect.height > size.GetHeight()) imeRect.y = size.GetHeight() - imeRect.height;

        // Update m_cursorRect so Render() stays in sync
        m_cursorRect = imeRect;

        // Set input box size and position to match cursor exactly
        m_imeInputBox->SetSize(imeRect.x, imeRect.y, imeRect.width, imeRect.height);

        // On macOS, notify input method about position
#ifdef __WXMAC__
        // Use NSTextInputClient to set first rect for line
        // This is a simplified approach - for production you might need more sophisticated handling
        // The SetSize call above should help with basic alignment
#endif
    }
}

void TermGLCanvas::Render() {
    if (!m_glInitialized) return;
    
    // 【强制上下文绑定】：不加这行，多窗口或初始化时上传的纹理在 Render 里就是隐形的！
    m_glContext->SetCurrent(*this);
    
    glClear(GL_COLOR_BUFFER_BIT);
    
    wxSize size = GetSize();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, size.GetWidth(), size.GetHeight(), 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    // Margin offset: 8px left/right, 4px top/bottom (DPI-scaled for high DPI screens)
    const float margin_x = 8.0f * m_dpiScale;
    const float margin_y = 4.0f * m_dpiScale;

    // Use cached cell width/height
    float cell_width = static_cast<float>(m_cellWidth);
    float cell_height = static_cast<float>(m_cellHeight);
    
    // 1. 绘制背景色 (不带纹理)
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    
    glBegin(GL_QUADS);
    for (const auto& cell : m_screen_cells) {
        // Skip continuation cells (char_code = 0 for wide character continuation)
        if (cell.char_code == 0) continue;

        float x = static_cast<int>(cell.cell_x) * cell_width + margin_x;
        float y = (static_cast<int>(cell.cell_y) - m_scroll_offset) * cell_height + margin_y;

        // Use cell width for wide characters
        uint8_t cell_width_multiplier = (cell.width > 0 && cell.width <= 2) ? cell.width : 1;
        float render_width = cell_width * cell_width_multiplier;

        // Tab character: widen by 6px (3px on each side)
        if (cell.char_code == '\t') {
            x -= 3.0f;
            render_width += 6.0f;
        }

        uint8_t bg_r = cell.bg_color & 0xFF;
        uint8_t bg_g = (cell.bg_color >> 8) & 0xFF;
        uint8_t bg_b = (cell.bg_color >> 16) & 0xFF;

        glColor3f(bg_r / 255.0f, bg_g / 255.0f, bg_b / 255.0f);
        glVertex2f(x, y);
        glVertex2f(x + render_width, y);
        glVertex2f(x + render_width, y + cell_height);
        glVertex2f(x, y + cell_height);
    }
    glEnd();
    
    // 1.5 绘制选择高亮
    if (m_selecting || (m_selection_start_row != m_selection_end_row || m_selection_start_col != m_selection_end_col)) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0.2f, 0.4f, 0.8f, 0.5f); // Darker blue with more transparency
        
        int start_row = std::min(m_selection_start_row, m_selection_end_row);
        int end_row = std::max(m_selection_start_row, m_selection_end_row);
        int start_col = std::min(m_selection_start_col, m_selection_end_col);
        int end_col = std::max(m_selection_start_col, m_selection_end_col);
        
        for (int row = start_row; row <= end_row; row++) {
            for (int col = start_col; col <= end_col; col++) {
                float x = col * cell_width + margin_x;
                float y = row * cell_height + margin_y;
                
                glBegin(GL_QUADS);
                    glVertex2f(x, y);
                    glVertex2f(x + cell_width, y);
                    glVertex2f(x + cell_width, y + cell_height);
                    glVertex2f(x, y + cell_height);
                glEnd();
            }
        }
        
        glDisable(GL_BLEND);
    }
    
    // 2. 绘制前景色文字 (带纹理映射)
    if (m_fontAtlas) {
        // Pre-warm all characters into atlas BEFORE glBegin, because AddCharToAtlas calls
        // glTexSubImage2D which must NOT be called inside a glBegin/glEnd block.
        for (const auto& cell : m_screen_cells) {
            if (cell.char_code == 0) continue;
            if (cell.char_code < 32 && cell.char_code != '\t') continue;
            char32_t render_char = (cell.char_code == '\t') ? ' ' : cell.char_code;
            m_fontAtlas->GetCharMetrics(render_char);
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, m_fontAtlas->GetTextureID());
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        
        glBegin(GL_QUADS);
        for (const auto& cell : m_screen_cells) {
            // Skip continuation cells (char_code = 0 for wide character continuation)
            if (cell.char_code == 0) continue;
            // Skip control chars except tab
            if (cell.char_code < 32 && cell.char_code != '\t') continue;

            float x = static_cast<int>(cell.cell_x) * cell_width + margin_x;
            float y = (static_cast<int>(cell.cell_y) - m_scroll_offset) * cell_height + margin_y;

            // Use cell width for wide characters (Chinese characters use 2 cells)
            uint8_t cell_width_multiplier = (cell.width > 0 && cell.width <= 2) ? cell.width : 1;
            float render_width = cell_width * cell_width_multiplier;

            // Tab character: widen by 6px (3px on each side)
            if (cell.char_code == '\t') {
                x -= 3.0f;
                render_width += 6.0f;
            }

            // Use space glyph for tab since tab (0x09) is not in font atlas
            char32_t render_char = (cell.char_code == '\t') ? ' ' : cell.char_code;
            CharMetrics metrics = m_fontAtlas->GetCharMetrics(render_char);
            if (metrics.u == 0 && metrics.v == 0 && metrics.w == 0 && metrics.h == 0) {
                if (cell.char_code > 127) {
                    SSH_LOG("Character not found in font atlas: 0x" << std::hex << cell.char_code << std::dec);
                }
                continue;
            }

            // 直接使用正确的 metrics 值
            float test_u1 = metrics.u;
            float test_v1 = metrics.v;
            float test_u2 = metrics.u + metrics.w;
            float test_v2 = metrics.v + metrics.h;

            // 使用真实的前景色
            uint8_t fg_r = cell.fg_color & 0xFF;
            uint8_t fg_g = (cell.fg_color >> 8) & 0xFF;
            uint8_t fg_b = (cell.fg_color >> 16) & 0xFF;
            glColor3f(fg_r / 255.0f, fg_g / 255.0f, fg_b / 255.0f);

            // Apply bearingX offset so left padding in atlas maps to correct screen position.
            // For tab, use 0 so the space glyph fills the widened tab area evenly (centered).
            float x_offset = (cell.char_code == '\t') ? 0.0f : metrics.bearingX;
            glTexCoord2f(test_u1, test_v1); glVertex2f(x + x_offset, y);
            glTexCoord2f(test_u2, test_v1); glVertex2f(x + render_width, y);
            glTexCoord2f(test_u2, test_v2); glVertex2f(x + render_width, y + cell_height);
            glTexCoord2f(test_u1, test_v2); glVertex2f(x + x_offset, y + cell_height);
        }
        glEnd();
        
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
    }
    
    // 3. 绘制光标 (直接使用 SetCursorPosition 中计算好的 m_cursorRect)
    if (m_cursor_visible) {
        glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBegin(GL_QUADS);
            glVertex2f(m_cursorRect.x, m_cursorRect.y);
            glVertex2f(m_cursorRect.x + m_cursorRect.width, m_cursorRect.y);
            glVertex2f(m_cursorRect.x + m_cursorRect.width, m_cursorRect.y + m_cursorRect.height);
            glVertex2f(m_cursorRect.x, m_cursorRect.y + m_cursorRect.height);
        glEnd();
        glDisable(GL_BLEND);
    }
    
    SwapBuffers();
}

void TermGLCanvas::OnPaint(wxPaintEvent& event) {
    static int paintCount = 0;
    if (paintCount == 0) {
        SSH_LOG("TermGLCanvas::OnPaint called for the first time");
    }
    paintCount++;
    
    // Initialize OpenGL on first paint
    if (!m_glInitialized) {
        InitializeGL();
    }
    
    // Set current context
    if (m_glContext) {
        m_glContext->SetCurrent(*this);
    }
    
    // Render
    Render();
}

void TermGLCanvas::OnSize(wxSizeEvent& event) {
    // Update cached cell height when size changes
    if (m_cellHeight > 0) {
        m_cached_cell_height = m_cellHeight;
    } else {
        m_cached_cell_height = std::max(12, m_charHeight);
    }
    
    // Update OpenGL viewport to cover entire window
    // Margin is already applied manually in rendering coordinates,
    // so viewport should be 1:1 with window pixel coordinates
    if (m_glInitialized && m_glContext) {
        m_glContext->SetCurrent(*this);
        wxSize size = GetSize();
        glViewport(0, 0, size.GetWidth(), size.GetHeight());
    }
    
    Refresh();
    event.Skip();
}

void TermGLCanvas::OnKeyDown(wxKeyEvent& event) {
    SSH_LOG("OnKeyDown: keycode=" << event.GetKeyCode() << ", key_callback=" << (key_callback_ ? "set" : "NULL"));

    // Check for Ctrl+C to copy selection
    if (event.ControlDown() && event.GetKeyCode() == 'C') {
        CopySelectionToClipboard();
        return;
    }

    if (key_callback_) {
        const char* sequence = nullptr;
        switch (event.GetKeyCode()) {
            case WXK_RETURN:
            case WXK_NUMPAD_ENTER:
                sequence = "\r";
                break;
            case WXK_UP:
                sequence = "\x1b[A";
                break;
            case WXK_DOWN:
                sequence = "\x1b[B";
                break;
            case WXK_RIGHT:
                sequence = "\x1b[C";
                break;
            case WXK_LEFT:
                sequence = "\x1b[D";
                break;
            case WXK_BACK:
                sequence = "\x7f"; // Backspace
                break;
            case WXK_DELETE:
                sequence = "\x1b[3~"; // Delete
                break;
            case WXK_TAB:
                sequence = "\t";
                break;
            default:
                break;
        }

        if (sequence) {
            SSH_LOG("  Sending sequence: " << sequence);
            key_callback_(sequence, static_cast<int>(strlen(sequence)));
            return;
        }
    }

    // For regular text input, let CHAR_HOOK handle it
    event.Skip();
}

void TermGLCanvas::OnChar(wxKeyEvent& event) {
    if (key_callback_) {
        // Get the Unicode key - this handles basic character input
        int keycode = event.GetUnicodeKey();

        if (keycode != WXK_NONE) {
            wxString str;
            str << (wxChar)keycode;
            wxCharBuffer buffer = str.ToUTF8();
            int len = buffer.length();

            if (len > 0) {
                // Send UTF-8 encoded character to terminal
                key_callback_(buffer.data(), len);
            }
        }
    }
    // Skip to allow repeat key events (important for holding down keys in vi mode)
    event.Skip();
}

void TermGLCanvas::OnCharHook(wxKeyEvent& event) {
    // Let OnChar handle the character
    event.Skip();
}

void TermGLCanvas::OnMouseWheel(wxMouseEvent& event) {
    int delta = event.GetWheelRotation();
    int lines = delta / event.GetWheelDelta();

    // Normal scrolling
    SSH_LOG("TermGLCanvas::OnMouseWheel: delta=" << delta << ", lines=" << lines);
    SSH_LOG("  scroll_callback_ is " << (scroll_callback_ ? "set" : "NULL"));

    // Call scroll callback if set
    if (scroll_callback_) {
        scroll_callback_(lines);
    } else {
        SSH_LOG("  WARNING: scroll_callback_ is NULL, scroll will not work");
    }

    Refresh();

    event.Skip();
}

void TermGLCanvas::OnMouseLeftDown(wxMouseEvent& event) {
    SSH_LOG("TermGLCanvas::OnMouseLeftDown called");
    
#ifdef _WIN32
    // On Windows, avoid SetFocus race with IME kill-focus handler.
    // wxWidgets will set focus to this canvas automatically; OnSetFocus
    // will then show the IME input box.
#else
    // On other platforms, keep original behavior
    SetFocus();
    ShowIMEInputBox();
#endif
    
    wxPoint pos = event.GetPosition();
    const float margin_x = 8.0f * m_dpiScale;
    const float margin_y = 4.0f * m_dpiScale;
    float cell_width = static_cast<float>(m_cellWidth);
    float cell_height = static_cast<float>(m_cellHeight);

    // Convert pixel position to cell coordinates
    int col = std::max(0, static_cast<int>((pos.x - margin_x) / cell_width));
    int row = std::max(0, static_cast<int>((pos.y - margin_y) / cell_height));
    
    // Send mouse event to callback (for vi mouse mode)
    if (mouse_callback_) {
        mouse_callback_(row, col, 0);  // 0 = left button
    }
    
    m_selecting = true;
    m_selection_start_row = row;
    m_selection_start_col = col;
    m_selection_end_row = row;
    m_selection_end_col = col;
    
    CaptureMouse();
    Refresh();
    event.Skip();
}

void TermGLCanvas::OnMouseLeftUp(wxMouseEvent& event) {
    if (m_selecting) {
        m_selecting = false;
        ReleaseMouse();
        Refresh();
    }
    event.Skip();
}

void TermGLCanvas::OnMouseMove(wxMouseEvent& event) {
    if (m_selecting && event.LeftIsDown()) {
        wxPoint pos = event.GetPosition();
        const float margin_x = 8.0f * m_dpiScale;
        const float margin_y = 4.0f * m_dpiScale;
        float cell_width = static_cast<float>(m_cellWidth);
        float cell_height = static_cast<float>(m_cellHeight);

        // Convert pixel position to cell coordinates
        int col = std::max(0, static_cast<int>((pos.x - margin_x) / cell_width));
        int row = std::max(0, static_cast<int>((pos.y - margin_y) / cell_height));
        
        m_selection_end_row = row;
        m_selection_end_col = col;
        
        Refresh();
    }
    event.Skip();
}

void TermGLCanvas::OnMouseRightDown(wxMouseEvent& event) {
    // Copy selection to clipboard on right click
    SSH_LOG("OnMouseRightDown called");
    CopySelectionToClipboard();
    event.Skip();
}

void TermGLCanvas::CopySelectionToClipboard() {
    SSH_LOG("CopySelectionToClipboard called");
    SSH_LOG("Selection: start(" << m_selection_start_row << "," << m_selection_start_col 
            << ") end(" << m_selection_end_row << "," << m_selection_end_col << ")");
    
    if (m_selection_start_row == m_selection_end_row && 
        m_selection_start_col == m_selection_end_col) {
        SSH_LOG("No selection, skipping copy");
        return; // No selection
    }
    
    // Determine selection bounds
    int start_row = std::min(m_selection_start_row, m_selection_end_row);
    int end_row = std::max(m_selection_start_row, m_selection_end_row);
    int start_col = std::min(m_selection_start_col, m_selection_end_col);
    int end_col = std::max(m_selection_start_col, m_selection_end_col);
    
    SSH_LOG("Selection bounds: rows " << start_row << "-" << end_row << ", cols " << start_col << "-" << end_col);
    
    // Build selected text
    wxString selected_text;
    
    for (int row = start_row; row <= end_row; row++) {
        for (int col = start_col; col <= end_col; col++) {
            int index = row * m_cols_count + col;
            bool found = false;
            char32_t charCode = ' ';
            
            if (index >= 0 && index < (int)m_screen_cells.size()) {
                const auto& cell = m_screen_cells[index];
                if ((int)cell.cell_x == col && (int)cell.cell_y == row) {
                    charCode = cell.char_code;
                    found = true;
                }
            }
            
            if (found) {
                if (charCode < 32) {
                    selected_text += ' ';
                } else if (charCode < 128) {
                    selected_text += static_cast<char>(charCode);
                } else {
                    // Convert Unicode (UTF-32) to UTF-8 for clipboard
                    wxString str;
                    str << static_cast<wxChar>(charCode);
                    wxCharBuffer buffer = str.ToUTF8();
                    if (buffer.length() > 0) {
                        selected_text += wxString::FromUTF8(buffer);
                    } else {
                        selected_text += ' ';
                    }
                }
            } else {
                selected_text += ' ';
            }
        }
        if (row < end_row) {
            selected_text += '\n';
        }
    }
    
    SSH_LOG("Selected text: '" << selected_text << "'");
    
    // Copy to clipboard
    if (wxTheClipboard->Open()) {
        SSH_LOG("Clipboard opened successfully");
        wxTheClipboard->SetData(new wxTextDataObject(selected_text));
        wxTheClipboard->Close();
        SSH_LOG("Copied selection to clipboard: " << selected_text.length() << " characters");
    } else {
        SSH_LOG("Failed to open clipboard");
    }
    
    // Clear selection after copy
    m_selecting = false;
    m_selection_start_row = 0;
    m_selection_start_col = 0;
    m_selection_end_row = 0;
    m_selection_end_col = 0;
    Refresh();
}

void TermGLCanvas::OnSetFocus(wxFocusEvent& event) {
    SSH_LOG("TermGLCanvas gained focus");
#ifdef _WIN32
    // Windows: show IME when canvas gains focus
    ShowIMEInputBox();
#endif
    event.Skip();
}

void TermGLCanvas::OnKillFocus(wxFocusEvent& event) {
    // Canvas lost focus
    SSH_LOG("TermGLCanvas lost focus");
    // Don't hide IME input box here to avoid focus loop
    event.Skip();
}

void TermGLCanvas::ShowIMEInputBox() {
    SSH_LOG("TermGLCanvas::ShowIMEInputBox called - m_cursor_col: " << m_cursor_col << ", m_cursor_row: " << m_cursor_row);
    SSH_LOG("  m_imeInputBox: " << (m_imeInputBox ? "exists" : "NULL") << ", m_imeInputBoxVisible: " << m_imeInputBoxVisible);

    if (!m_imeInputBox) {
        SSH_LOG("m_imeInputBox is null, cannot show IME input box");
        return;
    }

    if (m_imeInputBoxVisible) {
        SSH_LOG("IME input box already visible, skipping");
        return;
    }

    // Set callback to send text to terminal
    m_imeCallback = [this](const char* data, int length) {
        if (key_callback_) {
            key_callback_(data, length);
        }
    };
    SSH_LOG("IME callback set");

    // Use pre-calculated cursor rect so IME matches rendered cursor exactly
    wxRect imeRect = m_cursorRect;

    // Ensure it stays within canvas bounds (same logic as SetCursorPosition)
    wxSize canvasSize = GetSize();
    if (imeRect.x < 0) imeRect.x = 0;
    if (imeRect.y < 0) imeRect.y = 0;
    if (imeRect.x + imeRect.width > canvasSize.GetWidth()) imeRect.x = canvasSize.GetWidth() - imeRect.width;
    if (imeRect.y + imeRect.height > canvasSize.GetHeight()) imeRect.y = canvasSize.GetHeight() - imeRect.height;

    // Update m_cursorRect with bounded values so Render() stays in sync
    m_cursorRect = imeRect;

    // Set input box size and position to match cursor exactly
    m_imeInputBox->SetSize(imeRect.x, imeRect.y, imeRect.width, imeRect.height);
    m_imeInputBox->Show();
    m_imeInputBox->SetFocus();
    m_imeInputBoxVisible = true;

    SSH_LOG("IME input box shown at position: " << imeRect.x << ", " << imeRect.y << " with size: " << imeRect.width << "x" << imeRect.height);
}

void TermGLCanvas::HideIMEInputBox() {
    // SSH_LOG("TermGLCanvas::HideIMEInputBox called");
    if (m_imeInputBox && m_imeInputBoxVisible) {
        m_imeInputBox->Hide();
        m_imeInputBox->Clear();
        m_imeInputBoxVisible = false;
        m_imeCallback = nullptr;
        SSH_LOG("IME input box hidden");
    } else {
        SSH_LOG("IME input box not visible or null, skipping hide");
    }
}

void TermGLCanvas::OnProxyTextReceived(wxCommandEvent& event) {
    if (m_imeInputBox && m_imeCallback) {
        wxString content = m_imeInputBox->GetValue();
        if (content.IsEmpty()) return;

        // Extract the finalized UTF-8 string from IME
        wxCharBuffer buffer = content.ToUTF8();
        std::string utf8_str(buffer.data(), buffer.length());
        
        // Send clean data to terminal
        m_imeCallback(utf8_str.c_str(), utf8_str.length());

        // Trigger refresh to update rendering
        Refresh(false);

        // Clear proxy input box using ChangeValue to prevent triggering wxEVT_TEXT again
        m_imeInputBox->ChangeValue("");
    }
}

void TermGLCanvas::OnProxyKeyDown(wxKeyEvent& event) {
    int keycode = event.GetKeyCode();

    // Always handle control keys regardless of IME state
    if (keycode == WXK_RETURN || keycode == WXK_BACK || keycode == WXK_DELETE ||
        keycode == WXK_TAB || keycode == WXK_ESCAPE ||
        (keycode >= WXK_LEFT && keycode <= WXK_DOWN) ||
        keycode == WXK_HOME || keycode == WXK_END || keycode == WXK_PAGEUP || keycode == WXK_PAGEDOWN)
    {
        // Convert keycode to terminal escape sequence
        std::string key_seq;
        switch (keycode) {
            case WXK_RETURN:
                key_seq = "\r";
                break;
            case WXK_BACK:
                key_seq = "\x7f"; // Backspace
                break;
            case WXK_DELETE:
                key_seq = "\x1b[3~"; // Delete
                break;
            case WXK_TAB:
                key_seq = "\t";
                break;
            case WXK_ESCAPE:
                key_seq = "\x1b";
                break;
            case WXK_UP:
                key_seq = "\x1b[A";
                break;
            case WXK_DOWN:
                key_seq = "\x1b[B";
                break;
            case WXK_LEFT:
                key_seq = "\x1b[D";
                break;
            case WXK_RIGHT:
                key_seq = "\x1b[C";
                break;
            case WXK_HOME:
                if (event.ControlDown()) {
                    // Ctrl+Home: Scroll to top of scrollback
                    if (scroll_callback_) {
                        scroll_callback_(10000); // Large number to scroll to top
                    }
                    return;
                }
                key_seq = "\x1b[H";
                break;
            case WXK_END:
                if (event.ControlDown()) {
                    // Ctrl+End: Scroll to bottom
                    if (scroll_callback_) {
                        scroll_callback_(-10000); // Large negative number to scroll to bottom
                    }
                    return;
                }
                key_seq = "\x1b[F";
                break;
            case WXK_PAGEUP:
                key_seq = "\x1b[5~";
                break;
            case WXK_PAGEDOWN:
                key_seq = "\x1b[6~";
                break;
            default:
                break;
        }

        if (!key_seq.empty()) {
            SSH_LOG("  key_seq: " << key_seq << ", length: " << key_seq.length());
            if (m_imeCallback) {
                m_imeCallback(key_seq.c_str(), key_seq.length());
                SSH_LOG("  Sent via m_imeCallback");
            } else if (key_callback_) {
                // Fallback to key_callback if imeCallback is not set
                key_callback_(key_seq.c_str(), key_seq.length());
                SSH_LOG("  Sent via key_callback");
            } else {
                SSH_LOG("  ERROR: No callback available!");
            }
        }
        return;  // Don't skip - we handled it
    }

    // For normal characters, let IME handle them
    event.Skip();
}

void TermGLCanvas::OnIMETextLostFocus(wxFocusEvent& event) {
    // SSH_LOG("TermGLCanvas::OnIMETextLostFocus called");
#ifdef _WIN32
    // Windows: only hide IME if focus went to a window outside this canvas.
    // If focus returned to TermGLCanvas (e.g. mouse click), OnSetFocus
    // will re-show the IME, so don't hide it here to avoid flicker.
    wxWindow* focusWindow = wxWindow::FindFocus();
    if (focusWindow == this) {
        event.Skip();
        return;
    }
#endif
    HideIMEInputBox();
    event.Skip();
}