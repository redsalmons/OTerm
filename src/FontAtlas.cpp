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
    , m_fontSize(0)
    , m_currentTime(0) {
}

FontAtlas::~FontAtlas() {
    if (m_textureID != 0) {
        glDeleteTextures(1, &m_textureID);
    }
}

bool FontAtlas::InitializeSystemFont(int fontSize, const wxString& fontName) {
    SSHManager::init_log_file();
    SSH_LOG("FontAtlas::InitializeSystemFont called with fontSize=" << fontSize << ", fontName='" << fontName << "'");
    
    m_fontSize = fontSize;
    
    if (fontName.IsEmpty()) {
        // Use default font
        m_font = wxFont(fontSize, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
        SSH_LOG("FontAtlas: Using default modern font");
    } else {
        // Use specified font from config
        m_font = wxFont(fontSize, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, fontName);
        SSH_LOG("FontAtlas: Using specified font from config: " << fontName);
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
    int charSize = m_fontSize;
    
    m_textureWidth = 1024;
    m_textureHeight = 1024;
    
    SSH_LOG("FontAtlas: Creating empty texture atlas for dynamic loading: " << m_textureWidth << "x" << m_textureHeight);
    
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        SSH_LOG("FontAtlas: OpenGL error before texture creation: " << err);
    }
    
    glGenTextures(1, &m_textureID);
    if (m_textureID == 0) {
        SSH_LOG("FontAtlas: Failed to generate texture ID");
        return false;
    }
    SSH_LOG("FontAtlas: Generated texture ID: " << m_textureID);
    
    glBindTexture(GL_TEXTURE_2D, m_textureID);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        SSH_LOG("FontAtlas: OpenGL error after glBindTexture: " << err);
    }
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        SSH_LOG("FontAtlas: OpenGL error after glPixelStorei: " << err);
    }
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        SSH_LOG("FontAtlas: OpenGL error after glTexParameteri: " << err);
    }
    
    // Create empty texture with GL_RGBA format
    int totalPixels = m_textureWidth * m_textureHeight * 4;
    unsigned char* emptyData = new unsigned char[totalPixels];
    memset(emptyData, 0, totalPixels);
    
    SSH_LOG("FontAtlas: Calling glTexImage2D with width=" << m_textureWidth << " height=" << m_textureHeight);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_textureWidth, m_textureHeight, 
                 0, GL_RGBA, GL_UNSIGNED_BYTE, emptyData);
    
    err = glGetError();
    if (err != GL_NO_ERROR) {
        SSH_LOG("FontAtlas: OpenGL error creating empty texture: " << err);
        delete[] emptyData;
        return false;
    }
    
    delete[] emptyData;
    SSH_LOG("FontAtlas: Empty texture atlas created successfully, ID=" << m_textureID);
    
    // Initialize free space tracking
    m_freeSpaces.clear();
    m_freeSpaces.push_back(std::make_pair(0, 0));
    
    // Pre-load ASCII characters
    for (int i = 32; i <= 126; i++) {
        AddCharToAtlas(static_cast<char32_t>(i));
    }
    
    SSH_LOG("FontAtlas: Pre-loaded ASCII characters");
    return true;
}

bool FontAtlas::AddCharToAtlas(char32_t charCode) {
    // Check if character already exists
    auto it = m_charMetrics.find(charCode);
    if (it != m_charMetrics.end()) {
        it->second.last_used = m_currentTime++;
        return true;
    }
    
    // Convert charCode to wxString
    wxString text;
    if (charCode < 128) {
        text = wxString::Format("%c", static_cast<char>(charCode));
    } else {
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
    
    // Create bitmap for this character
    int charSize = m_fontSize;
    int charHeight = charSize;
    
    // Get actual character width from font using temporary DC
    wxBitmap tempBitmap(1, 1, 24);
    wxMemoryDC tempDC(tempBitmap);
    tempDC.SetFont(m_font);
    wxSize extent = tempDC.GetTextExtent(text);
    int charWidth = extent.x;
    if (charWidth == 0) charWidth = charSize; // Fallback if extent is 0
    tempDC.SelectObject(wxNullBitmap);
    
    wxBitmap bitmap(charWidth, charHeight, 24);
    wxMemoryDC dc(bitmap);
    
    dc.SetBackground(wxBrush(wxColour(0, 0, 0)));
    dc.Clear();
    dc.SetFont(m_font);
    dc.SetTextForeground(wxColour(255, 255, 255));
    
    // Draw character with vertical offset to avoid baseline clipping
    dc.DrawText(text, 0, -8);
    dc.SelectObject(wxNullBitmap);
    
    // Convert to image and then to GL_RGBA
    wxImage image = bitmap.ConvertToImage();
    if (!image.IsOk()) return false;
    
    unsigned char* rgbData = image.GetData();
    unsigned char* rgbaData = new unsigned char[charWidth * charHeight * 4];
    for (int i = 0; i < charWidth * charHeight; i++) {
        unsigned char r = rgbData[i * 3 + 0];
        unsigned char g = rgbData[i * 3 + 1];
        unsigned char b = rgbData[i * 3 + 2];
        unsigned char luminance = (r + g + b) / 3;
        if (luminance < 15) luminance = 0;
        rgbaData[i * 4 + 0] = luminance;
        rgbaData[i * 4 + 1] = luminance;
        rgbaData[i * 4 + 2] = luminance;
        rgbaData[i * 4 + 3] = luminance;
    }
    
    // Find free space in atlas
    int x, y;
    if (!FindFreeSpace(charWidth, charHeight, x, y)) {
        // No space available, evict LRU
        EvictLRU();
        if (!FindFreeSpace(charWidth, charHeight, x, y)) {
            SSH_LOG("FontAtlas: Failed to find space for character 0x" << std::hex << static_cast<uint32_t>(charCode) << std::dec);
            delete[] rgbaData;
            return false;
        }
    }
    
    // Update texture with full height
    UpdateTexture(x, y, charWidth, charHeight, rgbaData);
    
    // Store metrics
    CharMetrics metrics;
    metrics.u = static_cast<float>(x) / m_textureWidth;
    metrics.v = static_cast<float>(y) / m_textureHeight;
    metrics.w = static_cast<float>(charWidth) / m_textureWidth;
    metrics.h = static_cast<float>(charHeight) / m_textureHeight;
    metrics.advance = static_cast<float>(charWidth);
    metrics.bearingX = 0.0f;
    metrics.bearingY = 0.0f;
    metrics.actual_height = static_cast<float>(charHeight);
    metrics.last_used = m_currentTime++;
    metrics.tex_x = x;
    metrics.tex_y = y;
    metrics.tex_w = charWidth;
    metrics.tex_h = charHeight;
    
    m_charMetrics[charCode] = metrics;
    
    delete[] rgbaData;
    return true;
}

bool FontAtlas::FindFreeSpace(int charWidth, int charHeight, int& outX, int& outY) {
    if (m_freeSpaces.empty()) {
        return false;
    }
    
    auto& pos = m_freeSpaces.back();
    outX = pos.first;
    outY = pos.second;
    
    pos.first += charWidth + 2;
    if (pos.first + charWidth > m_textureWidth) {
        pos.first = 0;
        pos.second += charHeight + 2;
        if (pos.second + charHeight > m_textureHeight) {
            return false;
        }
    }
    
    return true;
}

void FontAtlas::EvictLRU() {
    char32_t oldestChar = 0;
    int oldestTime = m_currentTime;
    
    for (const auto& [charCode, metrics] : m_charMetrics) {
        if (metrics.last_used < oldestTime) {
            oldestTime = metrics.last_used;
            oldestChar = charCode;
        }
    }
    
    if (oldestChar != 0) {
        SSH_LOG("FontAtlas: Evicting LRU character 0x" << std::hex << static_cast<uint32_t>(oldestChar) << std::dec);
        const auto& metrics = m_charMetrics[oldestChar];
        
        int totalPixels = metrics.tex_w * metrics.tex_h * 4;
        unsigned char* clearData = new unsigned char[totalPixels];
        memset(clearData, 0, totalPixels);
        UpdateTexture(metrics.tex_x, metrics.tex_y, metrics.tex_w, metrics.tex_h, clearData);
        delete[] clearData;
        
        m_freeSpaces.push_back(std::make_pair(metrics.tex_x, metrics.tex_y));
        
        m_charMetrics.erase(oldestChar);
    }
}

void FontAtlas::UpdateTexture(int x, int y, int width, int height, unsigned char* data) {
    glBindTexture(GL_TEXTURE_2D, m_textureID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data);
    
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        SSH_LOG("FontAtlas: OpenGL error updating texture: " << err);
    }
}

CharMetrics FontAtlas::GetCharMetrics(char32_t charCode) {
    auto it = m_charMetrics.find(charCode);
    if (it != m_charMetrics.end()) {
        it->second.last_used = m_currentTime++;
        return it->second;
    }
    
    // Character not found, try to add it dynamically
    if (AddCharToAtlas(charCode)) {
        it = m_charMetrics.find(charCode);
        if (it != m_charMetrics.end()) {
            return it->second;
        }
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
    defaultMetrics.actual_height = static_cast<float>(m_fontSize);
    defaultMetrics.last_used = 0;
    defaultMetrics.tex_x = 0;
    defaultMetrics.tex_y = 0;
    defaultMetrics.tex_w = 0;
    defaultMetrics.tex_h = 0;
    
    return defaultMetrics;
}

unsigned int FontAtlas::GetTextureID() const {
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
