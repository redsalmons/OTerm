#include "TermGLCanvas.h"

#include "TerminalPanel.h"

#include "AppWindow.h"

#include <wx/wx.h>

#include <wx/display.h>

#include <wx/clipbrd.h>

#include <wx/popupwin.h>

#include <iostream>

#include <iomanip>

#include <algorithm>

#include <chrono>

#include <filesystem>

#ifdef __APPLE__

#include <OpenGL/gl.h>

#else

#include <GL/gl.h>

#endif

#include "SSHManager.h"

#include "GlobalConfig.h"
#include "MacIMEProxy.h"
#include "MacKeyMonitor.h"
#include "SSHTerminalContainer.h"
#include "TranslationHelper.h"



#ifndef GL_CLAMP_TO_EDGE

#define GL_CLAMP_TO_EDGE 0x812F

#endif



TermGLCanvas::TermGLCanvas(wxWindow* parent, bool createThread)

    : wxGLCanvas(parent, wxID_ANY, nullptr, wxDefaultPosition, wxDefaultSize,

                 wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS | wxCLIP_CHILDREN | wxBORDER_NONE),

      m_glContext(nullptr),

      m_fontAtlas(nullptr),

      m_testTextureID(0),

      m_rows_count(0), m_cols_count(0),

      m_cursor_row(0), m_cursor_col(0), m_cursor_visible(true), m_cursor_out_of_bounds(false), m_scroll_offset(0),

      m_cached_cell_height(24), m_glInitialized(false), m_dpiScale(1.0f),

      m_selecting(false), m_selection_start_row(0), m_selection_start_col(0),

      m_selection_end_row(0), m_selection_end_col(0),

      m_fontSize(0),

      m_charHeight(0),

      m_cellWidth(0),

      m_cellHeight(0),

      m_cursorRect(0, 0, 0, 0),

      m_imeInputBox(nullptr),

      m_imePopup(nullptr),

      m_imeInputBoxVisible(false),
#ifdef __APPLE__
      m_macKeyMonitorToken(nullptr),
#endif
      m_ownsThreads(false),
      m_threadsStopped(false),
      m_wheelRotationAccumulator(0),
      m_zoomWheelAccumulator(0) {

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
    
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    SSH_LOG("TermGLCanvas constructor called, this=" << this << ", ownsThreads=" << m_ownsThreads);

    Bind(wxEVT_PAINT, &TermGLCanvas::OnPaint, this);

    Bind(wxEVT_SIZE, &TermGLCanvas::OnSize, this);

    Bind(wxEVT_KEY_DOWN, &TermGLCanvas::OnKeyDown, this);

    Bind(wxEVT_CHAR, &TermGLCanvas::OnChar, this);

    Bind(wxEVT_CHAR_HOOK, &TermGLCanvas::OnCharHook, this);

    Bind(wxEVT_MOUSEWHEEL, &TermGLCanvas::OnMouseWheel, this);

    Bind(wxEVT_LEFT_DOWN, &TermGLCanvas::OnMouseLeftDown, this);

    Bind(wxEVT_LEFT_UP, &TermGLCanvas::OnMouseLeftUp, this);

    Bind(wxEVT_MOTION, &TermGLCanvas::OnMouseMove, this);

    // std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
    // if (f.is_open()) f << "[CANVAS] Binding events, ID_HORIZONTAL_SPLIT=" << ID_HORIZONTAL_SPLIT << " ID_VERTICAL_SPLIT=" << ID_VERTICAL_SPLIT << std::endl;

    

    Bind(wxEVT_RIGHT_DOWN, &TermGLCanvas::OnMouseRightDown, this);

    Bind(wxEVT_RIGHT_UP, &TermGLCanvas::OnMouseRightDown, this);

    Bind(wxEVT_SET_FOCUS, &TermGLCanvas::OnSetFocus, this);

    Bind(wxEVT_KILL_FOCUS, &TermGLCanvas::OnKillFocus, this);

    Bind(wxEVT_MENU, &TermGLCanvas::OnHorizontalSplit, this, ID_HORIZONTAL_SPLIT);

    Bind(wxEVT_MENU, &TermGLCanvas::OnVerticalSplit, this, ID_VERTICAL_SPLIT);

    Bind(wxEVT_MENU, &TermGLCanvas::OnClosePanel, this, ID_CLOSE_PANEL);

    

    // if (f.is_open()) f << "[CANVAS] Events bound successfully" << std::endl;



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

    // CHAR_HOOK fires before the native text view consumes navigation keys;
    // bound on the proxy itself so it is seen even when the box has focus.

    m_imeInputBox->Bind(wxEVT_CHAR_HOOK, &TermGLCanvas::OnCharHook, this);



    // Create local terminal thread by default for console if requested (no-op since single-threaded)
    if (createThread) {
        m_ownsThreads = false;
    }



    SSH_LOG("TermGLCanvas constructed, IsShown: " << IsShown() << ", IsEnabled: " << IsEnabled() << ", DPI scale: " << m_dpiScale);

}



void TermGLCanvas::StopThreads() {
    auto startTime = std::chrono::steady_clock::now();
    SSH_LOG("TermGLCanvas::StopThreads called (no-op - threads managed by TerminalPanel)");
    
    // Skip if already stopped
    if (m_threadsStopped) {
        SSH_LOG("Threads already stopped, skipping");
        return;
    }
    
    m_threadsStopped = true;
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
    SSH_LOG("TermGLCanvas::StopThreads completed, total time: " << totalDuration << "ms");
}



void TermGLCanvas::ReinitializeGLContext() {
    SSH_LOG("ReinitializeGLContext called");
    // Delete old context
    if (m_glContext) {
        delete m_glContext;
        m_glContext = nullptr;
    }
    
    // Reset initialization flag
    m_glInitialized = false;
    
    // Force reinitialization on next paint
    Refresh();
}


TermGLCanvas::~TermGLCanvas() {

    SSH_LOG("TermGLCanvas destructor start, this=" << this << ", m_ownsThreads=" << m_ownsThreads << ", m_threadsStopped=" << m_threadsStopped);

#ifdef __APPLE__
    MacKeyMonitor::Unregister(m_macKeyMonitorToken);
    m_macKeyMonitorToken = nullptr;
    MacIME::Disable(this);
#endif

    // Only call StopThreads if not already called
    if (!m_threadsStopped) {
        StopThreads();
    } else {
        SSH_LOG("Threads already stopped, skipping StopThreads in destructor");
    }

    delete m_fontAtlas;

    delete m_glContext;

    if (m_imePopup) {

        m_imePopup->Destroy();

        m_imePopup = nullptr;

        m_imeInputBox = nullptr;

    } else if (m_imeInputBox) {

        delete m_imeInputBox;

        m_imeInputBox = nullptr;

    }

    SSH_LOG("TermGLCanvas destructor end, this=" << this);

}



float TermGLCanvas::GLPixelScale() const {

#if defined(__WXMAC__) || defined(__WXMSW__)

    return 1.0f;

#else

    return m_dpiScale;

#endif

}

// Physical framebuffer scale: on Retina displays the GL surface is 2x the
// logical widget size. The viewport must be sized in physical pixels while
// the ortho projection stays in logical coordinates.
float TermGLCanvas::GLFramebufferScale() const {
#ifdef __WXMAC__
    float scale = GetContentScaleFactor();
    return scale > 0.0f ? scale : 1.0f;
#else
    return 1.0f;
#endif
}

#ifdef __APPLE__

void TermGLCanvas::EnsureMacKeyMonitor() {

    if (!m_macKeyMonitorToken) {
        m_macKeyMonitorToken = MacKeyMonitor::Register(this,
            [this](unsigned short keyCode, unsigned long modifierFlags) {
                return HandleMacRawKey(keyCode, modifierFlags);
            });
    }

    // Make the canvas's NSView the IME client so the proxy text control never
    // has to take focus (which is what eats navigation keys under an IME).
    MacIME::Handlers ime;
    ime.commit = [this](const std::string& s) { OnIMECommit(s); };
    ime.cursorRect = [this](double& x, double& y, double& w, double& h) {
        x = m_cursorRect.x; y = m_cursorRect.y;
        w = m_cursorRect.width; h = m_cursorRect.height;
    };
    MacIME::Enable(this, ime);

}

// Raw macOS virtual key codes for keys that NSTextView consumes via
// doCommandBySelector before wx can generate key events.
bool TermGLCanvas::HandleMacRawKey(unsigned short keyCode, unsigned long modifierFlags) {

    // Nav-key range seen at AppKit level — log even when we decline so the
    // log shows whether events died in the focus check or before the monitor.
    const bool isNavKey = (keyCode >= 114 && keyCode <= 126) || keyCode == 53;

    if (!m_imeInputBox || wxWindow::FindFocus() != m_imeInputBox) {
        if (isNavKey) {
        }
        return false;
    }

    const bool ctrl = (modifierFlags & (1UL << 18)) != 0; // NSEventModifierFlagControl
    const char* seq = nullptr;

    switch (keyCode) {

        case 126: seq = "\x1b[A";  break; // Up
        case 125: seq = "\x1b[B";  break; // Down
        case 124: seq = "\x1bOC";  break; // Right
        case 123: seq = "\x1bOD";  break; // Left
        case 116: seq = "\x1b[5~"; break; // PageUp
        case 121: seq = "\x1b[6~"; break; // PageDown
        case 117: seq = "\x1b[3~"; break; // Forward delete
        case 114: seq = "\x1b[2~"; break; // Insert/Help
        case 53:  seq = "\x1b";    break; // Escape
        case 115:                          // Home
            if (ctrl) {
                if (scroll_callback_) scroll_callback_(10000);
                return true;
            }
            seq = "\x1b[H";
            break;
        case 119:                          // End
            if (ctrl) {
                if (scroll_callback_) scroll_callback_(-10000);
                return true;
            }
            seq = "\x1b[F";
            break;
        default:
            return false;
    }

    if (key_callback_) {
        key_callback_(seq, static_cast<int>(strlen(seq)));
    }

    return true; // consume — keep the event away from the text view
}

#endif // __APPLE__



bool TermGLCanvas::CreateFontAtlas() {

    if (m_fontAtlas) {

        delete m_fontAtlas;

        m_fontAtlas = nullptr;

    }

    m_fontAtlas = new FontAtlas();

    std::string fontName = GlobalConfig::GetFontName();

    int fontSize = GlobalConfig::GetEffectiveFontSize();

    // Rasterize glyphs at the physical pixel size for the current DPI scale

    int terminalFontSize = static_cast<int>(fontSize * m_dpiScale + 0.5f);

    if (terminalFontSize <= 0) terminalFontSize = fontSize;

    if (terminalFontSize < 8) terminalFontSize = 8;

    if (terminalFontSize > 72) terminalFontSize = 72;

    m_fontSize = terminalFontSize;

    if (!m_fontAtlas->InitializeSystemFont(terminalFontSize, wxString::FromUTF8(fontName.c_str()))) {

        SSH_LOG("Failed to initialize font atlas with configured font");

        return false;

    }

    // Get actual character rendering height from font atlas

    m_charHeight = m_fontAtlas->GetCharHeight();

    if (m_charHeight == 0) {

        m_charHeight = terminalFontSize; // Fallback to font size

    }

    SSH_LOG("Font atlas initialized: texture ID=" << m_fontAtlas->GetTextureID()

            << ", size=" << m_fontAtlas->GetTextureWidth() << "x" << m_fontAtlas->GetTextureHeight()

            << ", charHeight=" << m_charHeight);

    return true;

}



void TermGLCanvas::ApplyFontZoom() {

    if (!m_glInitialized || !m_glContext) return;

    m_glContext->SetCurrent(*this);

    if (CreateFontAtlas()) {

        InitializeFontMetrics();

        // Ask the parent panel to recompute vterm rows/cols with the new cell size

        wxWindow* parent = GetParent();

        if (parent) {

            wxSizeEvent sizeEvent(parent->GetSize(), parent->GetId());

            wxPostEvent(parent, sizeEvent);

        }

    }

    Refresh();

}



static void CollectTermCanvases(wxWindow* root, std::vector<TermGLCanvas*>& out) {

    if (!root) return;

    if (TermGLCanvas* canvas = dynamic_cast<TermGLCanvas*>(root)) {

        out.push_back(canvas);

        return;

    }

    for (wxWindow* child : root->GetChildren()) {

        CollectTermCanvases(child, out);

    }

}



void TermGLCanvas::BroadcastFontZoom() {

    wxWindow* top = wxGetTopLevelParent(this);

    if (!top) top = wxTheApp->GetTopWindow();

    std::vector<TermGLCanvas*> canvases;

    CollectTermCanvases(top, canvases);

    for (TermGLCanvas* canvas : canvases) {

        canvas->ApplyFontZoom();

    }

}



bool TermGLCanvas::HandleZoomInput(const wxKeyEvent& event) {

#ifdef __WXMAC__

    const bool zoomMod = event.CmdDown();

#else

    const bool zoomMod = event.MetaDown();

#endif

    if (!zoomMod || event.AltDown()) return false;

    int keycode = event.GetKeyCode();

    // Some paths (IME/proxy controls, Cmd+Shift combos on macOS) deliver the

    // key with keycode 0; fall back to Unicode and raw virtual key codes.

    if (keycode == 0) {

        const long uni = static_cast<long>(event.GetUnicodeKey());

        if (uni > 0 && uni != WXK_NONE) keycode = static_cast<int>(uni);

    }

    if (keycode == 0) {

        switch (event.GetRawKeyCode()) {

#ifdef __WXMAC__

            case 0x1B: keycode = '-'; break;

            case 0x18: keycode = '='; break;

            case 0x1D: keycode = '0'; break;

            case 0x4E: keycode = WXK_NUMPAD_SUBTRACT; break;

            case 0x45: keycode = WXK_NUMPAD_ADD; break;

            case 0x52: keycode = WXK_NUMPAD0; break;

#else

            case 0xBD: keycode = '-'; break;

            case 0xBB: keycode = '='; break;

            case 0x30: keycode = '0'; break;

#endif

        }

    }

    int delta = 0;

    if (keycode == WXK_ADD || keycode == WXK_NUMPAD_ADD || keycode == '=' || keycode == '+') {

        delta = 1;

    } else if (keycode == WXK_SUBTRACT || keycode == WXK_NUMPAD_SUBTRACT || keycode == '-' || keycode == '_') {

        delta = -1;

    } else if (keycode == '0' || keycode == WXK_NUMPAD0) {

        GlobalConfig::ResetFontZoom();

        BroadcastFontZoom();

        return true;

    } else {

        return false;

    }

    GlobalConfig::AdjustFontZoom(delta);

    BroadcastFontZoom();

    return true;

}



void TermGLCanvas::InitializeGL() {

    if (m_glInitialized) return;

    

    // Create OpenGL context

    m_glContext = new wxGLContext(this);

    m_glContext->SetCurrent(*this);

    

    // Set up OpenGL state
    wxSize size = GetSize();
    float glScale = GLPixelScale();
    int physW = static_cast<int>(size.GetWidth() * glScale);
    int physH = static_cast<int>(size.GetHeight() * glScale);
    if (physW > 0 && physH > 0) {
        glViewport(0, 0, physW, physH);
    }

    glClearColor(0.04f, 0.04f, 0.04f, 1.0f);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);

    

    // Create font atlas with configured font and size
    if (!CreateFontAtlas()) {
        return;
    }



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

#ifdef __APPLE__
    EnsureMacKeyMonitor();
#endif

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
        int availableHeight = static_cast<int>(size.GetHeight() * GLPixelScale()) - scroll_margin_y * 2;
        int visible_rows = availableHeight / m_cached_cell_height;
        
        // Calculate scroll offset to keep cursor at bottom
        if (m_cursor_row >= visible_rows) {
            m_scroll_offset = m_cursor_row - visible_rows + 1;
        } else {
            m_scroll_offset = 0;
        }
    }

    // 2. Determine if cursor is within screen bounds
    int screen_row = row;
    if (vterm_scroll_offset > 0) {
        screen_row = row + vterm_scroll_offset;
    }

    int max_rows = (m_rows_count > 0) ? m_rows_count : 24;
    m_cursor_out_of_bounds = (vterm_scroll_offset > 0 && screen_row >= max_rows);

    // 3. Now calculate cursor rect with the correct updated m_scroll_offset
    float margin_x = 8.0f * m_dpiScale;
    float margin_y = 4.0f * m_dpiScale;
    float cell_width = static_cast<float>(m_cellWidth);
    float cell_height = static_cast<float>(m_cellHeight);

    float cursor_x = col * cell_width + margin_x;
    float cursor_y = (screen_row - m_scroll_offset) * cell_height + margin_y;
    float cursor_height = cell_height * 0.85f;
    float cursor_y_offset = (cell_height - cursor_height) / 2.0f;
    cursor_y += cursor_y_offset;

    // Save to member for Render() to use
    if (m_cursor_out_of_bounds) {
        m_cursorRect = wxRect(0, 0, 0, 0);
    } else {
        m_cursorRect = wxRect(static_cast<int>(cursor_x), static_cast<int>(cursor_y),
                              static_cast<int>(cell_width), static_cast<int>(cursor_height));
    }

    // 4. Update IME input box position and size if visible
    if (m_imeInputBox && m_imeInputBoxVisible) {
        if (m_cursor_out_of_bounds) {
            m_imeInputBox->Hide();
        } else {
            UpdateIMEBoxPlacement();
            m_imeInputBox->Show();
        }
    }
}



void TermGLCanvas::UpdateIMEBoxPlacement() {

    if (!m_imeInputBox) return;

    const float glScale = GLPixelScale();

    // m_cursorRect is in GL pixel space; clamp it there first

    wxSize physSize(static_cast<int>(GetSize().GetWidth() * glScale),
                    static_cast<int>(GetSize().GetHeight() * glScale));

    wxRect imeRect = m_cursorRect;

    if (imeRect.x < 0) imeRect.x = 0;
    if (imeRect.y < 0) imeRect.y = 0;
    if (imeRect.x + imeRect.width > physSize.GetWidth()) imeRect.x = physSize.GetWidth() - imeRect.width;
    if (imeRect.y + imeRect.height > physSize.GetHeight()) imeRect.y = physSize.GetHeight() - imeRect.height;

    // Update m_cursorRect so Render() stays in sync
    m_cursorRect = imeRect;

    // Convert back to logical pixels for native window placement
    wxRect logicalRect(static_cast<int>(imeRect.x / glScale),
                       static_cast<int>(imeRect.y / glScale),
                       static_cast<int>(imeRect.width / glScale),
                       static_cast<int>(imeRect.height / glScale));

    m_imeInputBox->SetSize(logicalRect);

}



void TermGLCanvas::Render() {

    if (!m_glInitialized) return;

    

    // 【强制上下文绑定】：不加这行，多窗口或初始化时上传的纹理在 Render 里就是隐形的！

    m_glContext->SetCurrent(*this);

    wxSize size = GetSize();

    float glScale = GLPixelScale();

    int physW = static_cast<int>(size.GetWidth() * glScale);

    int physH = static_cast<int>(size.GetHeight() * glScale);

    int fbScale = static_cast<int>(GLFramebufferScale());
    glViewport(0, 0, physW * fbScale, physH * fbScale);

    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);

    glLoadIdentity();

    glOrtho(0, physW, physH, 0, -1, 1);

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


    // 2.5 绘制下划线
    glBegin(GL_LINES);
    for (const auto& cell : m_screen_cells) {
        if (cell.char_code == 0) continue;
        if (cell.char_code < 32 && cell.char_code != '\t') continue;

        // Check if underline attribute is set (0x02)
        if (cell.attrs & 0x02) {
            float x = static_cast<int>(cell.cell_x) * cell_width + margin_x;
            float y = (static_cast<int>(cell.cell_y) - m_scroll_offset) * cell_height + margin_y;

            uint8_t cell_width_multiplier = (cell.width > 0 && cell.width <= 2) ? cell.width : 1;
            float render_width = cell_width * cell_width_multiplier;

            if (cell.char_code == '\t') {
                x -= 3.0f;
                render_width += 6.0f;
            }

            // Draw underline at baseline (near bottom of cell)
            float underline_y = y + cell_height - 2.0f;

            uint8_t fg_r = cell.fg_color & 0xFF;
            uint8_t fg_g = (cell.fg_color >> 8) & 0xFF;
            uint8_t fg_b = (cell.fg_color >> 16) & 0xFF;
            glColor3f(fg_r / 255.0f, fg_g / 255.0f, fg_b / 255.0f);

            glVertex2f(x, underline_y);
            glVertex2f(x + render_width, underline_y);
        }
    }
    glEnd();


    // 3. 绘制光标 (直接使用 SetCursorPosition 中计算好的 m_cursorRect)

    if (m_cursor_visible && !m_cursor_out_of_bounds) {

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

    // Must be created even though it is unused: on Windows it validates the

    // update region, otherwise WM_PAINT repeats and the canvas repaints in

    // incomplete patches.

    wxPaintDC dc(this);

    

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

        float glScale = GLPixelScale();

        int fbScale = static_cast<int>(GLFramebufferScale());
        glViewport(0, 0, static_cast<int>(size.GetWidth() * glScale * fbScale), static_cast<int>(size.GetHeight() * glScale * fbScale));

    }

    

    Refresh();

    event.Skip();

}



void TermGLCanvas::OnKeyDown(wxKeyEvent& event) {

    if (HandleZoomInput(event)) return;

    // Check for Ctrl+C to copy selection (only if there's a selection)

    if (event.GetKeyCode() == WXK_CONTROL_C || (event.ControlDown() && event.GetKeyCode() == 'C')) {

        if (m_selection_start_row != m_selection_end_row || m_selection_start_col != m_selection_end_col) {

            CopySelectionToClipboard();

            return;

        }

        // If no selection, clear SSH/local input buffer and let it fall through to send Ctrl+C to terminal
        TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
        if (panel) {
            if (panel->IsLocalTerminal()) {
                panel->ClearInputBuffer();
            } else {
                m_sshInputBuffer.clear();
            }
        }

    }



    // Check for Ctrl+V to paste from clipboard

    if (event.ControlDown() && event.GetKeyCode() == 'V') {

        if (wxTheClipboard->Open()) {

            if (wxTheClipboard->IsSupported(wxDF_TEXT)) {

                wxTextDataObject data;

                wxTheClipboard->GetData(data);

                wxString text = data.GetText();

                wxTheClipboard->Close();



                // Check if text length is <= 255

                if (text.length() <= 255) {

                    // Convert to UTF-8 and send to terminal

                    wxScopedCharBuffer utf8_buf = text.ToUTF8();

                    if (key_callback_) {

                        key_callback_(utf8_buf.data(), static_cast<int>(strlen(utf8_buf.data())));

                        SSH_LOG("Pasted " << strlen(utf8_buf.data()) << " characters from clipboard");

                        // Update input buffer for local/SSH terminals for command interception
                        TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
                        if (panel) {
                            std::string pasted_str(utf8_buf.data(), strlen(utf8_buf.data()));
                            if (panel->IsLocalTerminal()) {
                                panel->AppendToInputBuffer(pasted_str);
                                SSH_LOG("Pasted to input buffer: '" << pasted_str << "', buffer now='" << panel->GetInputBuffer() << "'");
                            } else {
                                m_sshInputBuffer += pasted_str;
                                SSH_LOG("Pasted to SSH input buffer: '" << pasted_str << "', m_sshInputBuffer now='" << m_sshInputBuffer << "'");
                            }
                        }

                    }

                } else {

                    SSH_LOG("Clipboard text too long (" << text.length() << " > 255), skipping paste");

                }

            } else {

                wxTheClipboard->Close();

                SSH_LOG("Clipboard does not contain text, skipping paste");

            }

        }

        return;

    }



    if (key_callback_) {

        const char* sequence = nullptr;

        switch (event.GetKeyCode()) {

            case WXK_RETURN:

            case WXK_NUMPAD_ENTER:
                {
                    // Check if this is a local terminal and command should be intercepted
                    TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
                    // If the session has ended, pressing Enter starts a new local terminal
                    if (panel) {
                        bool alive = panel->IsSessionAlive();
                        if (!alive) {
                            panel->RestartAsLocalTerminal();
                            return;
                        }
                    }
                    if (panel && panel->IsLocalTerminal()) {
                        if (panel->CheckAndHandleLocalCommand()) {
                            return;
                        }
                        sequence = "\r";
                        panel->ClearInputBuffer();
                    } else {
                        // std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
                        // if (f.is_open()) f << "[SSH_KEYDOWN] Enter pressed, m_sshInputBuffer: '" << m_sshInputBuffer << "'" << std::endl;
                        
                        if (!m_sshInputBuffer.empty()) {
                            std::string command = m_commandInterceptor.ExtractCommand(m_sshInputBuffer);
                            // if (f.is_open()) f << "[SSH_KEYDOWN] Extracted command: '" << command << "'" << std::endl;
                            
                            if (command == "download" || command == "upload") {
                                // if (f.is_open()) f << "[SSH_KEYDOWN] Intercepting upload/download, triggering file transfer" << std::endl;
                                
                                sequence = "\x03"; // Send Ctrl+C to cancel command line on remote shell
                                
                                wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED, wxID_ANY);
                                evt.SetInt(2); // Flag to indicate file transfer request
                                evt.SetString(wxString::FromUTF8(m_sshInputBuffer.c_str()));
                                evt.SetEventObject(this);
                                wxQueueEvent(panel, evt.Clone());
                            } else {
                                sequence = "\r";
                            }
                            m_sshInputBuffer.clear();
                        } else {
                            sequence = "\r";
                        }
                    }
                }

                break;

            case WXK_UP:

                sequence = "\x1b[A";

                break;

            case WXK_DOWN:

                sequence = "\x1b[B";

                break;

            case WXK_RIGHT:

                sequence = "\x1bOC";

                break;

            case WXK_LEFT:

                sequence = "\x1bOD";

                break;

            case WXK_BACK:
                {
                    sequence = "\x7f"; // Backspace
                    // Update input buffer for local/SSH terminals
                    TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
                    if (panel) {
                        if (panel->IsLocalTerminal()) {
                            const std::string& buffer = panel->GetInputBuffer();
                            if (!buffer.empty()) {
                                panel->AppendToInputBuffer("\x7f"); // Mark backspace
                                // Actually remove last character from buffer
                                std::string newBuffer = buffer;
                                if (!newBuffer.empty() && newBuffer.back() == '\x7f') {
                                    newBuffer.pop_back(); // Remove the backspace marker
                                }
                                if (!newBuffer.empty()) {
                                    newBuffer.pop_back(); // Remove last character
                                }
                                // Clear and set new buffer
                                panel->ClearInputBuffer();
                                if (!newBuffer.empty()) {
                                    panel->AppendToInputBuffer(newBuffer);
                                }
                            }
                        } else {
                            // SSH terminal: handle backspace for m_sshInputBuffer
                            if (!m_sshInputBuffer.empty()) {
                                // Remove one UTF-8 character (which could be multi-byte)
                                do {
                                    m_sshInputBuffer.pop_back();
                                } while (!m_sshInputBuffer.empty() && 
                                         ((unsigned char)m_sshInputBuffer.back() & 0xC0) == 0x80);
                            }
                        }
                    }
                }

                break;

            case WXK_DELETE:

                sequence = "\x1b[3~"; // Delete

                break;

            case WXK_TAB:

                sequence = "\t";

                break;

            case 'C':

            case WXK_CONTROL_C:

                if (event.ControlDown() || event.GetKeyCode() == WXK_CONTROL_C) {

                    sequence = "\x03"; // Ctrl+C = ETX (interrupt)

                }

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

    // Handle RETURN key for command interception
    if (event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER) {
        TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
        if (panel) {
            bool alive = panel->IsSessionAlive();
            // If the session has ended, pressing Enter starts a new local terminal
            if (!alive) {
                panel->RestartAsLocalTerminal();
                return;
            }
        }
        if (panel && panel->IsLocalTerminal()) {
            if (panel->CheckAndHandleLocalCommand()) {
                return;
            }
            panel->ClearInputBuffer();
        } else if (panel && !panel->IsLocalTerminal()) {
            // std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
            // if (f.is_open()) f << "[SSH_ONCHAR] Enter pressed, m_sshInputBuffer='" << m_sshInputBuffer << "'" << std::endl;

            // Check if SSH is disconnected and trigger reconnect
            SSHTerminalContainer* sshContainer = dynamic_cast<SSHTerminalContainer*>(panel->GetTerminalContainer());
            if (sshContainer && sshContainer->GetState() == SSHTerminalContainer::SSH_DISCONNECTED) {
                sshContainer->Connect();
                m_sshInputBuffer.clear();
                return;
            }

            if (!m_sshInputBuffer.empty()) {
                std::string command = m_commandInterceptor.ExtractCommand(m_sshInputBuffer);
                // if (f.is_open()) f << "[SSH_ONCHAR] Extracted command: '" << command << "'" << std::endl;

                if (command == "download" || command == "upload") {
                    // if (f.is_open()) f << "[SSH_ONCHAR] Intercepting upload/download, sending Ctrl+C" << std::endl;

                    if (key_callback_) {
                        key_callback_("\x03", 1);
                    }

                    wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED, wxID_ANY);
                    evt.SetInt(2); // Flag to indicate file transfer request
                    evt.SetString(wxString::FromUTF8(m_sshInputBuffer.c_str()));
                    evt.SetEventObject(this);
                    wxQueueEvent(panel, evt.Clone());

                    m_sshInputBuffer.clear();
                    return;
                }
                m_sshInputBuffer.clear();
            }
        }
    }

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

                // Update input buffer for local/SSH terminals for command interception
                TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
                if (panel) {
                    if (panel->IsLocalTerminal()) {
                        std::string utf8_str(buffer.data(), len);
                        panel->AppendToInputBuffer(utf8_str);
                    } else {
                        // SSH terminal: track in m_sshInputBuffer
                        // Only track printable characters (first byte >= 32 and != 127) or multi-byte leading bytes (> 127)
                        unsigned char first_char = (unsigned char)buffer.data()[0];
                        if ((first_char >= 32 && first_char != 127) || first_char > 127) {
                            std::string utf8_str(buffer.data(), len);
                            m_sshInputBuffer += utf8_str;
                        }
                    }
                }
            }

        }

    }

    // Skip to allow repeat key events (important for holding down keys in vi mode)

    event.Skip();

}



void TermGLCanvas::OnCharHook(wxKeyEvent& event) {

    // When the IME proxy box holds focus, the native text view swallows

    // navigation keys (arrows, Home/End, PageUp/Down, Delete, Escape) before a

    // wxEVT_KEY_DOWN is generated. Deliver them from the hook stage instead.

    if (m_imeInputBox && wxWindow::FindFocus() == m_imeInputBox) {

        const char* seq = nullptr;

        switch (event.GetKeyCode()) {

            case WXK_UP: seq = "\x1b[A"; break;

            case WXK_DOWN: seq = "\x1b[B"; break;

            case WXK_RIGHT: seq = "\x1bOC"; break;

            case WXK_LEFT: seq = "\x1bOD"; break;

            case WXK_PAGEUP: seq = "\x1b[5~"; break;

            case WXK_PAGEDOWN: seq = "\x1b[6~"; break;

            case WXK_DELETE: seq = "\x1b[3~"; break;

            case WXK_INSERT: seq = "\x1b[2~"; break;

            case WXK_ESCAPE: seq = "\x1b"; break;

            case WXK_HOME:

                if (event.ControlDown()) {

                    if (scroll_callback_) scroll_callback_(10000);

                    return;

                }

                seq = "\x1b[H";

                break;

            case WXK_END:

                if (event.ControlDown()) {

                    if (scroll_callback_) scroll_callback_(-10000);

                    return;

                }

                seq = "\x1b[F";

                break;

            default: break;

        }

        if (seq) {

            if (key_callback_) {

                key_callback_(seq, static_cast<int>(strlen(seq)));

            }

            return;

        }

    }

    // Handle RETURN key for command interception
    if (event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER) {
        TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
        if (panel) {
            bool alive = panel->IsSessionAlive();
            // If the session has ended, pressing Enter starts a new local terminal
            if (!alive) {
                panel->RestartAsLocalTerminal();
                return;
            }
        }
        if (panel && panel->IsLocalTerminal()) {
            if (panel->CheckAndHandleLocalCommand()) {
                return;
            }
        } else if (panel && !panel->IsLocalTerminal()) {
            // SSH terminal: check for upload/download commands
            // std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
            // if (f.is_open()) f << "[SSH_CHARHOOK] Enter pressed, m_sshInputBuffer='" << m_sshInputBuffer << "'" << std::endl;

            // Check if SSH is disconnected and trigger reconnect
            SSHTerminalContainer* sshContainer = dynamic_cast<SSHTerminalContainer*>(panel->GetTerminalContainer());
            if (sshContainer && sshContainer->GetState() == SSHTerminalContainer::SSH_DISCONNECTED) {
                sshContainer->Connect();
                m_sshInputBuffer.clear();
                return;
            }

            if (!m_sshInputBuffer.empty()) {
                std::string command = m_commandInterceptor.ExtractCommand(m_sshInputBuffer);
                // if (f.is_open()) f << "[SSH_CHARHOOK] Extracted command: '" << command << "'" << std::endl;

                if (command == "download" || command == "upload") {
                    // if (f.is_open()) f << "[SSH_CHARHOOK] Intercepting upload/download, sending Ctrl+C" << std::endl;

                    if (key_callback_) {
                        key_callback_("\x03", 1);
                    }

                    wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED, wxID_ANY);
                    evt.SetInt(2); // Flag to indicate file transfer request
                    evt.SetString(wxString::FromUTF8(m_sshInputBuffer.c_str()));
                    evt.SetEventObject(this);
                    wxQueueEvent(panel, evt.Clone());

                    m_sshInputBuffer.clear();
                    return; // Return without skipping to intercept
                }
                m_sshInputBuffer.clear();
            }
        }
    }

    event.Skip();

}



void TermGLCanvas::OnMouseWheel(wxMouseEvent& event) {
#ifdef __WXMAC__
    const bool zoomMod = event.CmdDown();
#else
    const bool zoomMod = event.MetaDown();
#endif
    if (zoomMod) {
        int wheelDelta = event.GetWheelDelta();
        if (wheelDelta <= 0) wheelDelta = 120;
        m_zoomWheelAccumulator += event.GetWheelRotation();
        int steps = m_zoomWheelAccumulator / wheelDelta;
        m_zoomWheelAccumulator %= wheelDelta;
        if (steps != 0) {
            GlobalConfig::AdjustFontZoom(steps);
            BroadcastFontZoom();
        }
        return;
    }
    int delta = event.GetWheelRotation();
    int wheelDelta = event.GetWheelDelta();
    if (wheelDelta <= 0) wheelDelta = 120; // fallback safety

    m_wheelRotationAccumulator += delta;
    int lines = m_wheelRotationAccumulator / wheelDelta;
    m_wheelRotationAccumulator %= wheelDelta;

    if (lines != 0) {
        if (scroll_callback_) {
            scroll_callback_(lines);
        } else {
            SSH_LOG("  WARNING: scroll_callback_ is NULL, scroll will not work");
        }
        Refresh();
    }

    event.Skip();
}



void TermGLCanvas::OnMouseLeftDown(wxMouseEvent& event) {

    

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

    int col = std::max(0, static_cast<int>((pos.x * GLPixelScale() - margin_x) / cell_width));

    int row = std::max(0, static_cast<int>((pos.y * GLPixelScale() - margin_y) / cell_height));

    

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

        int col = std::max(0, static_cast<int>((pos.x * GLPixelScale() - margin_x) / cell_width));

        int row = std::max(0, static_cast<int>((pos.y * GLPixelScale() - margin_y) / cell_height));

        

        m_selection_end_row = row;

        m_selection_end_col = col;

        

        Refresh();

    }

    event.Skip();

}



void TermGLCanvas::OnMouseRightDown(wxMouseEvent& event) {

    // std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
    // if (f.is_open()) f << "[CANVAS] OnMouseRightDown called" << std::endl;
    
    // Copy selection to clipboard on right click

    CopySelectionToClipboard();



    // Show context menu with split options

    wxMenu menu;

    menu.Append(ID_HORIZONTAL_SPLIT, TranslationHelper::Tr("horizontalSplit"));

    menu.Append(ID_VERTICAL_SPLIT, TranslationHelper::Tr("verticalSplit"));

    menu.AppendSeparator();

    menu.Append(ID_CLOSE_PANEL, TranslationHelper::Tr("closePanel"));

    

    // if (f.is_open()) f << "[CANVAS] Showing context menu at position: " << event.GetPosition().x << "," << event.GetPosition().y << std::endl;

    // if (f.is_open()) f << "[CANVAS] ID_HORIZONTAL_SPLIT=" << ID_HORIZONTAL_SPLIT << " ID_VERTICAL_SPLIT=" << ID_VERTICAL_SPLIT << std::endl;

    int result = PopupMenu(&menu, event.GetPosition());

    // if (f.is_open()) f << "[CANVAS] PopupMenu returned with result: " << result << std::endl;

    

    // Skip to let parent handle it too

    event.Skip();

}



void TermGLCanvas::OnHorizontalSplit(wxCommandEvent& event) {

    // std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
    // if (f.is_open()) f << "[CANVAS] OnHorizontalSplit called" << std::endl;
    
    if (split_callback_) {
        TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
        // if (f.is_open()) f << "[CANVAS] Using split_callback with wxSPLIT_HORIZONTAL, panel=" << panel << std::endl;

        split_callback_(wxSPLIT_HORIZONTAL, panel);

    } else {

        TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);

        if (panel) {
            // if (f.is_open()) f << "[CANVAS] Calling TerminalPanel::DoSplit with wxSPLIT_HORIZONTAL" << std::endl;

            panel->DoSplit(wxSPLIT_HORIZONTAL);

        } else {
            // if (f.is_open()) f << "[CANVAS] ERROR: Parent is not TerminalPanel" << std::endl;
        }

    }

}



void TermGLCanvas::OnVerticalSplit(wxCommandEvent& event) {

    // std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
    // if (f.is_open()) f << "[CANVAS] OnVerticalSplit called" << std::endl;
    
    if (split_callback_) {
        TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
        // if (f.is_open()) f << "[CANVAS] Using split_callback with wxSPLIT_VERTICAL, panel=" << panel << std::endl;
        split_callback_(wxSPLIT_VERTICAL, panel);
    } else {
        TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
        if (panel) {
            // if (f.is_open()) f << "[CANVAS] Calling TerminalPanel::DoSplit with wxSPLIT_VERTICAL" << std::endl;
            panel->DoSplit(wxSPLIT_VERTICAL);
        } else {
            // if (f.is_open()) f << "[CANVAS] ERROR: Parent is not TerminalPanel" << std::endl;
        }
    }

}

void TermGLCanvas::OnClosePanel(wxCommandEvent& event) {
    // std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
    // if (f.is_open()) f << "[CANVAS] OnClosePanel called" << std::endl;
    
    if (close_callback_) {
        TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
        // if (f.is_open()) f << "[CANVAS] Using close_callback, panel=" << panel << std::endl;
        close_callback_(panel);
    }
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

#ifdef _WIN32

    // Windows: show IME when canvas gains focus

    ShowIMEInputBox();

#endif

    event.Skip();

}



void TermGLCanvas::OnKillFocus(wxFocusEvent& event) {

    // Canvas lost focus

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

#ifdef __APPLE__
    EnsureMacKeyMonitor();
#endif

    if (m_imeInputBoxVisible) {

        SSH_LOG("IME input box already visible, skipping");

        return;

    }



    // Set callback to send text to terminal

    m_imeCallback = [this](const char* data, int length) {
        // Track input for SSH terminal upload/download command detection
        std::string modified_data;
        
        TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
        bool isSSH = panel && !panel->IsLocalTerminal();
        
        if (isSSH) {
            for (int i = 0; i < length; i++) {
                if (data[i] == '\r' || data[i] == '\n') {
                    // Enter key pressed - check if command is download or upload (SSH only)
                    if (!m_sshInputBuffer.empty()) {
                        // Debug log to alert file
                        // std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
                        // if (f.is_open()) f << "[SSH_INPUT] SSH command entered: " << m_sshInputBuffer << std::endl;
                        
                        SSH_LOG("SSH command entered: " << m_sshInputBuffer);
                        
                        // Extract the actual command (handle whitespace)
                        std::string command = m_commandInterceptor.ExtractCommand(m_sshInputBuffer);
                        
                        // Debug log to alert file
                        // if (f.is_open()) f << "[SSH_INPUT] Extracted command: '" << command << "'" << std::endl;
                        
                        SSH_LOG("Extracted command: '" << command << "'");
                        
                        if (command == "download" || command == "upload") {
                            // Debug log to alert file
                            // if (f.is_open()) f << "[SSH_INPUT] Command matches download/upload, triggering file transfer" << std::endl;
                            
                            SSH_LOG("Command matches download/upload, triggering file transfer");
                            modified_data += '\x03';
                            SSH_LOG("Detected upload/download command, showing file transfer dialog");
                            
                            // Send event to TerminalPanel which will forward it to find ConnectInfo
                            TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
                            if (panel) {
                                SSH_LOG("Found TerminalPanel, sending file transfer request");
                                wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED, wxID_ANY);
                                evt.SetInt(2); // Flag to indicate file transfer request
                                evt.SetString(m_sshInputBuffer);
                                evt.SetEventObject(this);
                                wxQueueEvent(panel, evt.Clone());
                            } else {
                                SSH_LOG("ERROR: Could not find TerminalPanel parent");
                            }
                        } else {
                            // Not upload/download, pass through normally
                            modified_data += data[i];
                        }
                        m_sshInputBuffer.clear();
                    } else {
                        modified_data += data[i];
                    }
                } else if (data[i] == 127 || data[i] == 8) {
                    // Backspace - remove last character
                    if (!m_sshInputBuffer.empty()) {
                        m_sshInputBuffer.pop_back();
                    }
                    modified_data += data[i];
                } else if (data[i] >= 32 && data[i] < 127) {
                    // Printable character
                    m_sshInputBuffer += data[i];
                    modified_data += data[i];
                } else {
                    // Other control characters
                    modified_data += data[i];
                }
            }
            
            if (key_callback_) {
                key_callback_(modified_data.c_str(), modified_data.length());
            }
        } else {
            // Local terminal: pass through directly
            if (key_callback_) {
                key_callback_(data, length);
            }
        }

    };

    SSH_LOG("IME callback set");

#ifdef __APPLE__
    // On macOS the canvas's own NSView is the NSTextInputClient target
    // (MacIMEProxy). The proxy text control is never shown or focused here —
    // that is what made the text system swallow navigation keys under an IME.
    return;
#endif

    // Position the box on the cursor (clamps m_cursorRect to canvas bounds)

    UpdateIMEBoxPlacement();

    m_imeInputBox->Show();
    m_imeInputBox->SetFocus();

    m_imeInputBoxVisible = true;



    SSH_LOG("IME input box shown at position: " << m_cursorRect.x << ", " << m_cursorRect.y << " with size: " << m_cursorRect.width << "x" << m_cursorRect.height);

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



void TermGLCanvas::OnIMECommit(const std::string& utf8) {

    if (utf8.empty()) return;

    if (m_imeCallback) {

        m_imeCallback(utf8.c_str(), static_cast<int>(utf8.size()));

    } else if (key_callback_) {

        key_callback_(utf8.c_str(), static_cast<int>(utf8.size()));

    }

    TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
    if (panel && panel->IsLocalTerminal()) {
        panel->AppendToInputBuffer(utf8);
    }

    Refresh(false);

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

        // Update input buffer for local terminals for command interception
        TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
        if (panel && panel->IsLocalTerminal()) {
            panel->AppendToInputBuffer(utf8_str);
            SSH_LOG("OnProxyTextReceived: appended '" << utf8_str << "' to input buffer, buffer now='" << panel->GetInputBuffer() << "'");
        }



        // Trigger refresh to update rendering

        Refresh(false);



        // Clear proxy input box using ChangeValue to prevent triggering wxEVT_TEXT again

        m_imeInputBox->ChangeValue("");

    }

}



void TermGLCanvas::OnProxyKeyDown(wxKeyEvent& event) {

    int keycode = event.GetKeyCode();

    if (HandleZoomInput(event)) return;

    SSH_LOG("OnProxyKeyDown called, keycode=" << keycode
              << " ctrl=" << event.ControlDown()
              << " rawctrl=" << event.RawControlDown()
              << " cmd=" << event.CmdDown()
              << " meta=" << event.MetaDown()
              << " alt=" << event.AltDown()
              << " shift=" << event.ShiftDown());

    // Handle Ctrl+C / Cmd+C on macOS
    bool isControlC = (event.ControlDown() || event.RawControlDown()) && (keycode == 'C' || keycode == WXK_CONTROL_C);
    bool isCmdC = event.CmdDown() && keycode == 'C';
    if (isControlC || isCmdC) {
        SSH_LOG("OnProxyKeyDown: Ctrl/Cmd+C detected, sending ETX");
        TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
        if (panel && panel->IsLocalTerminal()) {
            panel->ClearInputBuffer();
        }
        std::string key_seq = "\x03";
        if (m_imeCallback) {
            m_imeCallback(key_seq.c_str(), key_seq.length());
        } else if (key_callback_) {
            key_callback_(key_seq.c_str(), key_seq.length());
        }
        return;
    }

    // Handle backspace for command interception
    if (keycode == WXK_BACK) {
        TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
        if (panel && panel->IsLocalTerminal()) {
            SSH_LOG("OnProxyKeyDown: BACK pressed on local terminal, IsLocalTerminal=" << panel->IsLocalTerminal());
            const std::string& buffer = panel->GetInputBuffer();
            SSH_LOG("OnProxyKeyDown: current buffer='" << buffer << "'");
            if (!buffer.empty()) {
                std::string newBuffer = buffer;
                newBuffer.pop_back();
                panel->ClearInputBuffer();
                if (!newBuffer.empty()) {
                    panel->AppendToInputBuffer(newBuffer);
                }
                SSH_LOG("OnProxyKeyDown: buffer after backspace='" << panel->GetInputBuffer() << "'");
            }
        }
    }

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
                {
                    // Check if this is a local terminal and command should be intercepted
                    TerminalPanel* panel = wxDynamicCast(GetParent(), TerminalPanel);
                    if (panel && panel->IsLocalTerminal()) {
                        if (panel->CheckAndHandleLocalCommand()) {
                            return;
                        }
                        key_seq = "\r";
                        panel->ClearInputBuffer();
                    } else {
                        key_seq = "\r";
                    }
                }

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

                key_seq = "\x1bOD";

                break;

            case WXK_RIGHT:

                key_seq = "\x1bOC";

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