#include "TermGLCanvas.h"
#include <wx/wx.h>
#include <wx/display.h>
#include <iostream>
#include <iomanip>
#include <GL/gl.h>
#include "SSHManager.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

TermGLCanvas::TermGLCanvas(wxWindow* parent)
    : wxGLCanvas(parent, wxID_ANY, nullptr, wxDefaultPosition, wxDefaultSize, 
                 wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS | wxCLIP_CHILDREN | wxBORDER_NONE),
      m_glContext(nullptr),
      m_fontAtlas(nullptr),
      m_testTextureID(0),
      m_cursor_row(0), m_cursor_col(0), m_cursor_visible(true), m_scroll_offset(0), 
      m_cached_cell_height(24), m_glInitialized(false), m_dpiScale(1.0f)
{
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
    Bind(wxEVT_MOUSEWHEEL, &TermGLCanvas::OnMouseWheel, this);
    
    SSH_LOG("TermGLCanvas constructed, IsShown: " << IsShown() << ", IsEnabled: " << IsEnabled() << ", DPI scale: " << m_dpiScale);
}

TermGLCanvas::~TermGLCanvas() {
    delete m_fontAtlas;
    delete m_glContext;
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
    
    // Set default texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Create font atlas
    m_fontAtlas = new FontAtlas();
    if (!m_fontAtlas->InitializeSystemFont(48)) {
        SSH_LOG("Failed to initialize font atlas");
        return;
    }
    
    SSH_LOG("Font atlas initialized: texture ID=" << m_fontAtlas->GetTextureID() 
            << ", size=" << m_fontAtlas->GetTextureWidth() << "x" << m_fontAtlas->GetTextureHeight());
    
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

void TermGLCanvas::UpdateScreenData(const std::vector<CellInstance>& instances) {
    // Accumulate cell updates
    for (const auto& cell : instances) {
        std::pair<int, int> key = {(int)cell.cell_x, (int)cell.cell_y};
        m_screen_cells[key] = cell;
    }
    
    // Trigger refresh
    Refresh();
}

void TermGLCanvas::ClearScreenData() {
    m_screen_cells.clear();
}

void TermGLCanvas::SetCursorPosition(int row, int col) {
    m_cursor_row = row;
    m_cursor_col = col;
    
    // Calculate scroll offset to keep cursor visible
    wxSize size = GetSize();
    int visible_rows = size.GetHeight() / m_cached_cell_height;
    
    // Calculate scroll offset to keep cursor at bottom
    if (m_cursor_row >= visible_rows) {
        m_scroll_offset = m_cursor_row - visible_rows + 1;
    } else {
        m_scroll_offset = 0;
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
    
    // Margin offset: 8px left/right, 4px top/bottom (not DPI-scaled for rendering)
    const float margin_x = 8.0f;
    const float margin_y = 4.0f;
    
    // 1. 绘制背景色 (不带纹理)
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    
    for (const auto& [key, cell] : m_screen_cells) {
        float x = static_cast<int>(cell.cell_x) * 12.0f + margin_x;
        float y = static_cast<int>(cell.cell_y) * 24.0f + margin_y;
        
        uint8_t bg_r = (cell.bg_color >> 24) & 0xFF;
        uint8_t bg_g = (cell.bg_color >> 16) & 0xFF;
        uint8_t bg_b = (cell.bg_color >> 8) & 0xFF;
        
        glColor3f(bg_r / 255.0f, bg_g / 255.0f, bg_b / 255.0f);
        glBegin(GL_QUADS);
            glVertex2f(x, y);
            glVertex2f(x + 12.0f, y);
            glVertex2f(x + 12.0f, y + 24.0f);
            glVertex2f(x, y + 24.0f);
        glEnd();
    }
    
    // 2. 绘制前景色文字 (带纹理映射)
    if (m_fontAtlas) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, m_fontAtlas->GetTextureID());
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        
        for (const auto& [key, cell] : m_screen_cells) {
            if (cell.char_code < 32 || cell.char_code > 126) continue;

            float x = static_cast<int>(cell.cell_x) * 12.0f + margin_x;
            float y = static_cast<int>(cell.cell_y) * 24.0f + margin_y;

            CharMetrics metrics = m_fontAtlas->GetCharMetrics(cell.char_code);
            if (metrics.u == 0 && metrics.v == 0 && metrics.w == 0 && metrics.h == 0) continue;

            // 直接使用正确的 metrics 值
            float test_u1 = metrics.u;
            float test_v1 = metrics.v;
            float test_u2 = metrics.u + metrics.w;
            float test_v2 = metrics.v + metrics.h;

            // 使用真实的前景色
            uint8_t fg_r = (cell.fg_color >> 24) & 0xFF;
            uint8_t fg_g = (cell.fg_color >> 16) & 0xFF;
            uint8_t fg_b = (cell.fg_color >> 8) & 0xFF;
            glColor3f(fg_r / 255.0f, fg_g / 255.0f, fg_b / 255.0f);

            glBegin(GL_QUADS);
                glTexCoord2f(test_u1, test_v1); glVertex2f(x, y);
                glTexCoord2f(test_u2, test_v1); glVertex2f(x + 12.0f, y);
                glTexCoord2f(test_u2, test_v2); glVertex2f(x + 12.0f, y + 24.0f);
                glTexCoord2f(test_u1, test_v2); glVertex2f(x, y + 24.0f);
            glEnd();
        }
        
        
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
    }
    
    // 3. 绘制光标
    if (m_cursor_visible) {
        float cursor_x = m_cursor_col * 12.0f + margin_x;
        float cursor_y = m_cursor_row * 24.0f + margin_y;
        glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBegin(GL_QUADS);
            glVertex2f(cursor_x, cursor_y);
            glVertex2f(cursor_x + 12.0f, cursor_y);
            glVertex2f(cursor_x + 12.0f, cursor_y + 24.0f);
            glVertex2f(cursor_x, cursor_y + 24.0f);
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
    m_cached_cell_height = 24; // Fixed for now
    
    // Update OpenGL viewport with 8px left/right, 4px top/bottom margin
    if (m_glInitialized && m_glContext) {
        m_glContext->SetCurrent(*this);
        wxSize size = GetSize();
        const int margin_x = 8;
        const int margin_y = 4;
        glViewport(margin_x, margin_y, size.GetWidth() - margin_x * 2, size.GetHeight() - margin_y * 2);
    }
    
    Refresh();
    event.Skip();
}

void TermGLCanvas::OnKeyDown(wxKeyEvent& event) {
    // Let the system handle special keys
    event.Skip();
}

void TermGLCanvas::OnChar(wxKeyEvent& event) {
    if (key_callback_) {
        int keycode = event.GetUnicodeKey();
        if (keycode != WXK_NONE) {
            char data[4];
            int len = 0;
            if (keycode < 128) {
                data[0] = (char)keycode;
                len = 1;
            } else {
                // For Unicode characters, convert to UTF-8
                wxString str = wxString::FromUTF8((const char*)&keycode, 1);
                len = str.length();
                if (len > 0 && len <= 4) {
                    memcpy(data, str.mb_str(), len);
                } else {
                    // Fallback
                    data[0] = (char)keycode;
                    len = 1;
                }
            }
            if (len > 0) {
                key_callback_(data, len);
            }
        }
    }
    event.Skip();
}

void TermGLCanvas::OnMouseWheel(wxMouseEvent& event) {
    int delta = event.GetWheelRotation();
    int lines = delta / event.GetWheelDelta();
    
    SSH_LOG("TermGLCanvas::OnMouseWheel: delta=" << delta << ", lines=" << lines);
    
    // Call scroll callback if set
    if (scroll_callback_) {
        scroll_callback_(lines);
    }
    
    Refresh();
    event.Skip();
}