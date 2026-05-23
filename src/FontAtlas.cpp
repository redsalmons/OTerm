#include "FontAtlas.h"
#include <GL/gl.h>
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

bool FontAtlas::InitializeSystemFont(int fontSize) {
    m_fontSize = fontSize;
    m_font = wxFont(fontSize, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    
    if (!m_font.IsOk()) {
        SSH_LOG("FontAtlas: Failed to create font with size " << fontSize);
        return false;
    }
    
    SSH_LOG("FontAtlas: Font created successfully, size=" << fontSize);
    return GenerateTextureAtlas();
}

bool FontAtlas::GenerateTextureAtlas() {
    int charCount = 95; 
    int charSize = 64; // 使用固定尺寸 64，匹配字符高度
    
    m_textureWidth = 512;
    m_textureHeight = 1024; // 增大高度以容纳所有字符
    
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
    
    for (int i = 32; i <= 126; i++) {
        char32_t charCode = static_cast<char32_t>(i);
        AddCharToAtlas(charCode, dc, x, y, rowHeight);
        
        x += charSize;
        if (x + charSize > m_textureWidth) {
            x = 0;
            y += rowHeight;
        }
    }
    
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
        text = wxString::FromUTF8(reinterpret_cast<const char*>(&charCode), 4);
    }
    
    // Get character metrics
    wxSize extent = dc.GetTextExtent(text);
    int charWidth = extent.x;
    int charHeight = extent.y;
    
    // DEBUG: Log first few characters
    static int loggedCount = 0;
    if (loggedCount < 3) {
        SSH_LOG("FontAtlas: Char '" << text.ToStdString() << "' extent=" << charWidth << "x" << charHeight 
                << " pos=(" << x << "," << y << ")");
        loggedCount++;
    }
    
    // Draw character
    dc.DrawText(text, x + 2, y + 2); // 恢复 padding
    
    // Store metrics (使用实际的字符尺寸)
    CharMetrics metrics;
    metrics.u = static_cast<float>(x) / m_textureWidth;
    metrics.v = static_cast<float>(y) / m_textureHeight;
    metrics.w = static_cast<float>(charWidth) / m_textureWidth; // 使用实际宽度
    metrics.h = static_cast<float>(charHeight) / m_textureHeight; // 使用实际高度
    metrics.advance = static_cast<float>(charWidth);
    metrics.bearingX = 0.0f;
    metrics.bearingY = static_cast<float>(charHeight);
    
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
