#include "FontAtlas.h"
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <wx/dcmemory.h>
#include "SSHManager.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

FontAtlas::FontAtlas()
    : m_textureID(0)
    , m_textureWidth(0)
    , m_textureHeight(0)
    , m_fontSize(0) {
}

FontAtlas::~FontAtlas() {
    if (m_textureID != 0) {
        glDeleteTextures(1, &m_textureID);
    }
}

bool FontAtlas::InitializeSystemFont(int fontSize, const wxString& fontName) {
    m_fontSize = fontSize;
    
    if (fontName.IsEmpty()) {
        // Use default modern font if no font name specified
        m_font = wxFont(fontSize, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    } else {
        // Use specified font name
        m_font = wxFont(fontSize, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, fontName);
    }
    
    if (!m_font.IsOk()) {
        SSH_LOG("FontAtlas: Failed to create font with name '" << fontName << "' and size " << fontSize);
        // Fallback to default font
        m_font = wxFont(fontSize, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
        if (!m_font.IsOk()) {
            SSH_LOG("FontAtlas: Fallback font also failed");
            return false;
        }
    }
    
    SSH_LOG("FontAtlas: Font created successfully, name='" << m_font.GetFaceName() << "', size=" << fontSize);
    return GenerateTextureAtlas();
}

bool FontAtlas::GenerateTextureAtlas() {
    int charCount = 95;
    int charSize = m_fontSize; // Use configured font size
    
    // Increase texture size to support Unicode characters including Chinese
    // ASCII (32-126) + CJK Unified Ideographs (U+4E00-U+9FFF) subset
    m_textureWidth = 8192;
    m_textureHeight = 8192; // Larger texture for Unicode support
    
    SSH_LOG("FontAtlas: Generating texture atlas: " << m_textureWidth << "x" << m_textureHeight);
    
    // 【修改核心】：强行使用 24 位纯 RGB 位图，避开不可控的 Alpha 通道透明度污染
    wxBitmap bitmap(m_textureWidth, m_textureHeight, 24);
    wxMemoryDC dc(bitmap);
    
    // 绝对不透明的纯黑底
    dc.SetBackground(wxBrush(wxColour(0, 0, 0)));
    dc.Clear();
    
    // 绝对不透明的纯白字
    dc.SetFont(m_font);
    dc.SetTextForeground(wxColour(255, 255, 255));
    
    int x = 0;
    int y = 0;
    int rowHeight = charSize;
    int rowGap = 8; // Increase vertical gap to prevent bleeding for Chinese characters
    
    // Add ASCII characters (32-126)
    for (int i = 32; i <= 126; i++) {
        char32_t charCode = static_cast<char32_t>(i);
        AddCharToAtlas(charCode, dc, x, y, rowHeight);

        x += charSize;
        if (x + charSize > m_textureWidth) {
            x = 0;
            y += rowHeight + rowGap;
        }
    }

    // Add common Chinese characters (CJK Unified Ideographs)
    // Adding a comprehensive range of frequently used Chinese characters
    // U+4E00 to U+9FFF (CJK Unified Ideographs basic range)
    int chars_added = 0;
    for (char32_t charCode = 0x4E00; charCode <= 0x9FFF; charCode++) {
        AddCharToAtlas(charCode, dc, x, y, rowHeight);
        chars_added++;

        x += charSize;
        if (x + charSize > m_textureWidth) {
            x = 0;
            y += rowHeight + rowGap;
        }
        if (y + rowHeight > m_textureHeight) {
            SSH_LOG("FontAtlas: Texture atlas full, stopped at char: 0x" << std::hex << static_cast<uint32_t>(charCode) << std::dec << " after adding " << chars_added << " Chinese characters");
            break;
        }
    }
    SSH_LOG("FontAtlas: Added " << chars_added << " Chinese characters to texture atlas");
    
    // 【关键修复】：在转成 wxImage 之前，必须解绑 wxBitmap
    dc.SelectObject(wxNullBitmap);
    
    wxImage image = bitmap.ConvertToImage();
    if (!image.IsOk()) return false;
    
    unsigned char* rgbData = image.GetData();
    int pixelCount = m_textureWidth * m_textureHeight;
    
    // 构建标准的 RGBA 纹理
    unsigned char* rgbaData = new unsigned char[pixelCount * 4];
    int validPixels = 0;
    
    for (int i = 0; i < pixelCount; i++) {
        unsigned char r = rgbData[i * 3 + 0];
        unsigned char g = rgbData[i * 3 + 1];
        unsigned char b = rgbData[i * 3 + 2];
        
        // 亮度即为字形
        unsigned char luminance = (r + g + b) / 3;
        if (luminance < 15) luminance = 0; // 过滤噪声底色
        
        // 字本身是纯白的，由 Alpha 通道负责裁剪出字形
        rgbaData[i * 4 + 0] = 255; 
        rgbaData[i * 4 + 1] = 255; 
        rgbaData[i * 4 + 2] = 255; 
        rgbaData[i * 4 + 3] = luminance; 
        
        if (luminance > 20) validPixels++;
    }
    
    SSH_LOG("FontAtlas: Valid text pixels extracted: " << validPixels);
    
    // 创建 OpenGL 纹理
    glGenTextures(1, &m_textureID);
    glBindTexture(GL_TEXTURE_2D, m_textureID);
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_textureWidth, m_textureHeight, 
                 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaData);
    
    delete[] rgbaData;
    return true;
}

void FontAtlas::AddCharToAtlas(char32_t charCode, wxDC& dc, int& x, int& y, int rowHeight) {
    wxString text;
    if (charCode < 128) {
        text = wxString::Format("%c", static_cast<char>(charCode));
    } else {
        // Convert UTF-32 to UTF-8 for wxWidgets
        char utf8_buf[5] = {0};
        if (charCode <= 0x7F) {
            utf8_buf[0] = static_cast<char>(charCode);
        } else if (charCode <= 0x7FF) {
            utf8_buf[0] = 0xC0 | ((charCode >> 6) & 0x1F);
            utf8_buf[1] = 0x80 | (charCode & 0x3F);
        } else if (charCode <= 0xFFFF) {
            utf8_buf[0] = 0xE0 | ((charCode >> 12) & 0x0F);
            utf8_buf[1] = 0x80 | ((charCode >> 6) & 0x3F);
            utf8_buf[2] = 0x80 | (charCode & 0x3F);
        } else {
            utf8_buf[0] = 0xF0 | ((charCode >> 18) & 0x07);
            utf8_buf[1] = 0x80 | ((charCode >> 12) & 0x3F);
            utf8_buf[2] = 0x80 | ((charCode >> 6) & 0x3F);
            utf8_buf[3] = 0x80 | (charCode & 0x3F);
        }
        text = wxString::FromUTF8(utf8_buf);
    }
    
    // Get character metrics
    wxSize extent = dc.GetTextExtent(text);
    int charWidth = extent.x;
    int charHeight = extent.y;

    // Draw character at top of cell with padding
    // Adjust vertical offset for Chinese characters to align baseline with English
    int draw_x = x + 2;
    int draw_y = y + 2;
    if (charCode > 127) {
        // Chinese characters need vertical adjustment to align with English baseline
        draw_y += 2; // Shift down slightly for Chinese characters
    }
    dc.DrawText(text, draw_x, draw_y);

    // Store metrics using actual draw size, no padding cropping
    CharMetrics metrics;
    metrics.u = static_cast<float>(x) / m_textureWidth;
    metrics.v = static_cast<float>(y) / m_textureHeight;
    metrics.w = static_cast<float>(charWidth) / m_textureWidth;
    metrics.h = static_cast<float>(charHeight) / m_textureHeight;
    metrics.advance = static_cast<float>(charWidth);
    metrics.bearingX = 0.0f;
    metrics.bearingY = 0.0f;

    m_charMetrics[charCode] = metrics;
}

CharMetrics FontAtlas::GetCharMetrics(char32_t charCode) const {
    auto it = m_charMetrics.find(charCode);
    if (it != m_charMetrics.end()) {
        return it->second;
    }
    
    // Return default metrics for unknown characters
    CharMetrics defaultMetrics;
    defaultMetrics.u = 0.0f;
    defaultMetrics.v = 0.0f;
    defaultMetrics.w = 0.0f;
    defaultMetrics.h = 0.0f;
    defaultMetrics.advance = static_cast<float>(m_fontSize);
    defaultMetrics.bearingX = 0.0f;
    defaultMetrics.bearingY = static_cast<float>(m_fontSize);
    
    return defaultMetrics;
}

GLuint FontAtlas::GetTextureID() const {
    return m_textureID;
}

int FontAtlas::GetTextureWidth() const {
    return m_textureWidth;
}

int FontAtlas::GetTextureHeight() const {
    return m_textureHeight;
}

int FontAtlas::GetFontSize() const {
    return m_fontSize;
}
