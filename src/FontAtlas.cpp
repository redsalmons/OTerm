#include "FontAtlas.h"
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <wx/dcmemory.h>
#include <wx/dcgraph.h>
#include <memory>
#include <mutex>
#include <vector>
#include "SSHManager.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

struct GlyphBitmap {
    int width = 0;   // bitmap pixels incl. left padding
    int height = 0;
    int advance = 0; // advance without padding
    int bearingX = 0;
    std::vector<unsigned char> rgba;
};

namespace {

// Process-wide raster cache: "face#size" -> char -> pixels. Rasterizing a
// glyph via wxDC is the expensive part and is identical for every canvas,
// so the result is shared; each FontAtlas only uploads to its own texture.
std::mutex s_glyphMutex;
std::unordered_map<std::string, std::unordered_map<char32_t, std::shared_ptr<GlyphBitmap>>> s_glyphCache;
constexpr size_t kGlyphCacheLimitPerFont = 20000;

wxString CharToWxString(char32_t charCode) {
    if (charCode < 128) {
        return wxString::Format("%c", static_cast<char>(charCode));
    }
    char utf8_buf[5] = {0};
    if (charCode <= 0x7FF) {
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
    return wxString::FromUTF8(utf8_buf);
}

// Rasterizes glyphs reusing one measurement DC instead of creating two
// DC + bitmap pairs per character.
class GlyphRasterizer {
public:
    GlyphRasterizer(const wxFont& font, int charHeight, int fontSize)
        : m_font(font), m_charHeight(charHeight), m_fontSize(fontSize) {
        m_measureBitmap.Create(1, 1, 24);
        m_measureDC.SelectObject(m_measureBitmap);
        m_measureGCDC = new wxGCDC(m_measureDC);
        m_measureGCDC->SetFont(m_font);
    }

    ~GlyphRasterizer() {
        delete m_measureGCDC;
        m_measureDC.SelectObject(wxNullBitmap);
    }

    static int LeftPadding() {
#ifdef _WIN32
        return 2;
#else
        return 0;
#endif
    }

    int YOffset(const wxGCDC& dc, const wxString& text) const {
        wxSize ce = dc.GetTextExtent(text);
        int yOffset;
#ifdef _WIN32
        // On Windows, shift text up to leave space at bottom for underline
        yOffset = (m_charHeight - 12 - ce.y) / 2;
#else
        yOffset = (m_charHeight - ce.y) / 2;
#endif
        return yOffset < 0 ? 0 : yOffset;
    }

    static void ExtractRGBA(const unsigned char* src, size_t srcStride,
                            unsigned char* dst, int width, int height) {
        for (int y = 0; y < height; ++y) {
            const unsigned char* row = src + y * srcStride;
            unsigned char* d = dst + y * width * 4;
            for (int x = 0; x < width; ++x) {
                unsigned char lum = (unsigned char)((row[x * 3] + row[x * 3 + 1] + row[x * 3 + 2]) / 3);
                if (lum < 15) lum = 0;
                d[x * 4 + 0] = lum;
                d[x * 4 + 1] = lum;
                d[x * 4 + 2] = lum;
                d[x * 4 + 3] = lum;
            }
        }
    }

    GlyphBitmap Rasterize(const wxString& text) {
        wxSize extent = m_measureGCDC->GetTextExtent(text);
        int charWidth = extent.x ? extent.x : m_fontSize;
        const int leftPadding = LeftPadding();
        const int bitmapWidth = charWidth + leftPadding;

        wxBitmap bitmap(bitmapWidth, m_charHeight, 32);
        wxMemoryDC memDC(bitmap);
        memDC.SetBackground(wxBrush(wxColour(0, 0, 0)));
        memDC.Clear();
        {
            wxGCDC dc(memDC);
            dc.SetFont(m_font);
            dc.SetTextForeground(wxColour(255, 255, 255));
            dc.DrawText(text, leftPadding, YOffset(dc, text));
        }
        memDC.SelectObject(wxNullBitmap);

        GlyphBitmap gb;
        gb.width = bitmapWidth;
        gb.height = m_charHeight;
        gb.advance = charWidth;
        gb.bearingX = -leftPadding;
        wxImage image = bitmap.ConvertToImage();
        if (!image.IsOk()) return gb;
        gb.rgba.resize(bitmapWidth * m_charHeight * 4);
        ExtractRGBA(image.GetData(), (size_t)bitmapWidth * 3,
                    gb.rgba.data(), bitmapWidth, m_charHeight);
        return gb;
    }

    // Rasterize many characters in one bitmap + DC pass; returns entries in
    // the same order. On failure fewer (or zero) entries may come back.
    std::vector<std::pair<char32_t, std::shared_ptr<GlyphBitmap>>> RasterizeBatch(
            const std::vector<std::pair<char32_t, wxString>>& chars) {
        const int leftPadding = LeftPadding();
        struct Slot { int w, x; };
        std::vector<Slot> slots(chars.size());
        int totalW = 0;
        for (size_t i = 0; i < chars.size(); ++i) {
            wxSize e = m_measureGCDC->GetTextExtent(chars[i].second);
            slots[i].w = (e.x ? e.x : m_fontSize) + leftPadding;
            slots[i].x = totalW;
            totalW += slots[i].w;
        }
        if (totalW <= 0) totalW = 1;

        wxBitmap bitmap(totalW, m_charHeight, 32);
        wxMemoryDC memDC(bitmap);
        memDC.SetBackground(wxBrush(wxColour(0, 0, 0)));
        memDC.Clear();
        {
            wxGCDC dc(memDC);
            dc.SetFont(m_font);
            dc.SetTextForeground(wxColour(255, 255, 255));
            for (size_t i = 0; i < chars.size(); ++i) {
                dc.DrawText(chars[i].second, slots[i].x + leftPadding,
                            YOffset(dc, chars[i].second));
            }
        }
        memDC.SelectObject(wxNullBitmap);

        std::vector<std::pair<char32_t, std::shared_ptr<GlyphBitmap>>> out;
        wxImage image = bitmap.ConvertToImage();
        if (!image.IsOk()) return out;
        const unsigned char* rgb = image.GetData();
        for (size_t i = 0; i < chars.size(); ++i) {
            auto gb = std::make_shared<GlyphBitmap>();
            gb->width = slots[i].w;
            gb->height = m_charHeight;
            gb->advance = slots[i].w - leftPadding;
            gb->bearingX = -leftPadding;
            gb->rgba.resize(gb->width * gb->height * 4);
            ExtractRGBA(rgb + slots[i].x * 3, (size_t)totalW * 3,
                        gb->rgba.data(), gb->width, gb->height);
            out.emplace_back(chars[i].first, gb);
        }
        return out;
    }

private:
    wxFont m_font;
    int m_charHeight;
    int m_fontSize;
    wxBitmap m_measureBitmap;
    wxMemoryDC m_measureDC;
    wxGCDC* m_measureGCDC;
};

} // namespace

FontAtlas::FontAtlas()
    : m_textureID(0)
    , m_textureWidth(0)
    , m_textureHeight(0)
    , m_fontSize(0)
    , m_charHeight(0)
    , m_currentTime(0) {
    for (int i = 0; i < 128; ++i) {
        m_asciiMetricsCached[i] = false;
    }
}

FontAtlas::~FontAtlas() {
    if (m_textureID != 0) {
        glDeleteTextures(1, &m_textureID);
    }
}

bool FontAtlas::InitializeSystemFont(int fontSize, const wxString& fontName) {
    InitSSHLogFile();
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

    // Calculate actual character height including descenders
    // Use a temporary DC to get font metrics
    wxBitmap tempBitmap(1, 1, 24);
    wxMemoryDC tempDC(tempBitmap);
    wxGCDC gcdc(tempDC); // Use GCDC for accurate font metrics
    gcdc.SetFont(m_font);

    // Get text extent for a string containing ascenders and descenders
    wxString testChar = "Mg|jpqy_";
    wxSize extent = gcdc.GetTextExtent(testChar);

    // Use the actual character height including descenders
    m_charHeight = extent.y;

#ifdef _WIN32
    // On Windows, add padding to character height to provide space for underline rendering
    m_charHeight += 4;
#endif

    tempDC.SelectObject(wxNullBitmap);

    // Ensure minimum height
    if (m_charHeight < fontSize) {
        m_charHeight = fontSize;
    }

    m_cacheKey = m_font.GetFaceName().ToStdString() + "#" + std::to_string(m_fontSize);

    SSH_LOG("FontAtlas: Font created successfully, name='" << m_font.GetFaceName() << "', size=" << fontSize << ", charHeight=" << m_charHeight);
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
    
    delete[] emptyData;
    
    err = glGetError();
    if (err != GL_NO_ERROR) {
        SSH_LOG("FontAtlas: OpenGL error creating empty texture: " << err);
        return false;
    }
    
    SSH_LOG("FontAtlas: Empty texture atlas created successfully, ID=" << m_textureID);
    
    // Initialize free space tracking
    m_freeSpaces.clear();
    m_freeSpaces.push_back(std::make_pair(0, 0));
    
    // Pre-load ASCII characters: rasterize in one pass and share the bitmaps
    // process-wide; later atlases only upload from the cache.
    {
        std::vector<std::pair<char32_t, wxString>> missing;
        {
            std::lock_guard<std::mutex> lock(s_glyphMutex);
            auto& byChar = s_glyphCache[m_cacheKey];
            for (char32_t c = 32; c <= 126; ++c) {
                if (!byChar.count(c)) missing.emplace_back(c, CharToWxString(c));
            }
        }
        if (!missing.empty()) {
            GlyphRasterizer rasterizer(m_font, m_charHeight, m_fontSize);
            auto glyphs = rasterizer.RasterizeBatch(missing);
            std::lock_guard<std::mutex> lock(s_glyphMutex);
            auto& byChar = s_glyphCache[m_cacheKey];
            for (auto& g : glyphs) {
                byChar[g.first] = std::move(g.second);
            }
        }
        for (char32_t c = 32; c <= 126; ++c) {
            std::shared_ptr<GlyphBitmap> gb;
            {
                std::lock_guard<std::mutex> lock(s_glyphMutex);
                auto it = s_glyphCache[m_cacheKey].find(c);
                if (it != s_glyphCache[m_cacheKey].end()) gb = it->second;
            }
            if (gb) {
                UploadGlyph(c, gb->width, gb->height, gb->advance, gb->bearingX, gb->rgba.data());
            }
        }
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

    std::shared_ptr<GlyphBitmap> glyph;
    {
        std::lock_guard<std::mutex> lock(s_glyphMutex);
        auto& byChar = s_glyphCache[m_cacheKey];
        auto git = byChar.find(charCode);
        if (git != byChar.end()) glyph = git->second;
    }
    if (!glyph) {
        GlyphRasterizer rasterizer(m_font, m_charHeight, m_fontSize);
        glyph = std::make_shared<GlyphBitmap>(rasterizer.Rasterize(CharToWxString(charCode)));
        std::lock_guard<std::mutex> lock(s_glyphMutex);
        auto& byChar = s_glyphCache[m_cacheKey];
        if (byChar.size() < kGlyphCacheLimitPerFont) {
            byChar[charCode] = glyph;
        }
    }

    return UploadGlyph(charCode, glyph->width, glyph->height,
                       glyph->advance, glyph->bearingX, glyph->rgba.data());
}

bool FontAtlas::UploadGlyph(char32_t charCode, int bitmapWidth, int bitmapHeight,
                            int advance, int bearingX, const unsigned char* rgba) {
    if (bitmapWidth <= 0 || bitmapHeight <= 0 || !rgba) return false;

    // Find free space in atlas
    int x, y;
    if (!FindFreeSpace(bitmapWidth, bitmapHeight, x, y)) {
        // No space available, evict LRU
        EvictLRU();
        if (!FindFreeSpace(bitmapWidth, bitmapHeight, x, y)) {
            SSH_LOG("FontAtlas: Failed to find space for character 0x" << std::hex << static_cast<uint32_t>(charCode) << std::dec);
            return false;
        }
    }

    UpdateTexture(x, y, bitmapWidth, bitmapHeight, rgba);

    // Store metrics
    CharMetrics metrics;
    metrics.u = static_cast<float>(x) / m_textureWidth;
    metrics.v = static_cast<float>(y) / m_textureHeight;
    metrics.w = static_cast<float>(bitmapWidth) / m_textureWidth;
    metrics.h = static_cast<float>(bitmapHeight) / m_textureHeight;
    metrics.advance = static_cast<float>(advance);
    metrics.bearingX = static_cast<float>(bearingX);
    metrics.bearingY = 0.0f;
    metrics.actual_height = static_cast<float>(bitmapHeight);
    metrics.last_used = m_currentTime++;
    metrics.tex_x = x;
    metrics.tex_y = y;
    metrics.tex_w = bitmapWidth;
    metrics.tex_h = bitmapHeight;

    m_charMetrics[charCode] = metrics;
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

void FontAtlas::UpdateTexture(int x, int y, int width, int height, const unsigned char* data) {
    glBindTexture(GL_TEXTURE_2D, m_textureID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data);
    
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        SSH_LOG("FontAtlas: OpenGL error updating texture: " << err);
    }
}

CharMetrics FontAtlas::GetCharMetrics(char32_t charCode) {
    if (charCode < 128) {
        if (m_asciiMetricsCached[charCode]) {
            return m_asciiMetrics[charCode];
        }
    }

    auto it = m_charMetrics.find(charCode);
    if (it != m_charMetrics.end()) {
        it->second.last_used = m_currentTime++;
        CharMetrics metrics = it->second;
        if (charCode < 128) {
            m_asciiMetrics[charCode] = metrics;
            m_asciiMetricsCached[charCode] = true;
        }
        return metrics;
    }
    
    // Character not found, try to add it dynamically
    if (AddCharToAtlas(charCode)) {
        it = m_charMetrics.find(charCode);
        if (it != m_charMetrics.end()) {
            CharMetrics metrics = it->second;
            if (charCode < 128) {
                m_asciiMetrics[charCode] = metrics;
                m_asciiMetricsCached[charCode] = true;
            }
            return metrics;
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

int FontAtlas::GetCharHeight() const {
    return m_charHeight;
}
