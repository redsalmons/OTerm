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
#include <algorithm>
#include "SSHManager.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

struct GlyphBitmap {
    int width = 0;   // Cropped bitmap dimensions in physical pixels
    int height = 0;
    int advance = 0; // Measured advance, independent of the bitmap's ink bounds
    float bearingX = 0;
    float bearingY = 0;
    std::vector<unsigned char> rgba;
};

namespace {

// Process-wide raster cache: "face#size#scale" -> char -> pixels. Rasterizing a
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
    GlyphRasterizer(const wxFont& font, int charHeight, int baseline, float rasterScale)
        : m_font(font), m_charHeight(charHeight), m_baseline(baseline),
          m_rasterScale(rasterScale) {
        m_measureBitmap.Create(1, 1, 24);
        m_measureDC.SelectObject(m_measureBitmap);
        m_measureGCDC = new wxGCDC(m_measureDC);
        m_measureGCDC->SetFont(m_font);
    }

    ~GlyphRasterizer() {
        delete m_measureGCDC;
        m_measureDC.SelectObject(wxNullBitmap);
    }

    static void ExtractRGBA(const unsigned char* src, size_t srcStride,
                            unsigned char* dst, int width, int height) {
        for (int y = 0; y < height; ++y) {
            const unsigned char* row = src + y * srcStride;
            unsigned char* d = dst + y * width * 4;
            for (int x = 0; x < width; ++x) {
                unsigned char lum = (unsigned char)((row[x * 3] + row[x * 3 + 1] + row[x * 3 + 2]) / 3);
                // Straight alpha: GL_SRC_ALPHA applies coverage exactly once.
                d[x * 4 + 0] = 255;
                d[x * 4 + 1] = 255;
                d[x * 4 + 2] = 255;
                d[x * 4 + 3] = lum;
            }
        }
    }

    GlyphBitmap Rasterize(const wxString& text) {
        auto glyphs = RasterizeBatch({{0, text}});
        return glyphs.empty() ? GlyphBitmap{} : std::move(*glyphs.front().second);
    }

    // Rasterize many characters in one bitmap + DC pass; returns entries in
    // the same order. On failure fewer (or zero) entries may come back.
    std::vector<std::pair<char32_t, std::shared_ptr<GlyphBitmap>>> RasterizeBatch(
            const std::vector<std::pair<char32_t, wxString>>& chars) {
        // Text extents describe advances, not ink bounds. Reserve one line
        // height around each run for overhangs, then keep only its visible ink.
        const int padding = m_charHeight;
        struct Slot { int w, h, x, advance, top, drawY; };
        std::vector<Slot> slots(chars.size());
        int totalW = 0;
        int totalH = 0;
        for (size_t i = 0; i < chars.size(); ++i) {
            int width, height, descent;
            m_measureGCDC->GetTextExtent(chars[i].second, &width, &height, &descent);
            const int textTop = m_baseline - (height - descent);
            const int top = std::min(0, textTop) - padding;
            const int bottom = std::max(m_charHeight, m_baseline + descent) + padding;
            slots[i] = {width + 2 * padding, bottom - top, totalW,
                        width, top, textTop - top};
            totalW += slots[i].w;
            totalH = std::max(totalH, slots[i].h);
        }
        if (chars.empty()) return {};

        wxBitmap bitmap;
        bitmap.CreateWithLogicalSize(totalW, totalH, m_rasterScale, 32);
        wxMemoryDC memDC(bitmap);
        memDC.SetBackground(wxBrush(wxColour(0, 0, 0)));
        memDC.Clear();
        {
            wxGCDC dc(memDC);
            dc.SetFont(m_font);
            dc.SetTextForeground(wxColour(255, 255, 255));
            for (size_t i = 0; i < chars.size(); ++i) {
                dc.DrawText(chars[i].second, slots[i].x + padding, slots[i].drawY);
            }
        }
        memDC.SelectObject(wxNullBitmap);

        std::vector<std::pair<char32_t, std::shared_ptr<GlyphBitmap>>> out;
        wxImage image = bitmap.ConvertToImage();
        if (!image.IsOk()) return out;
        const unsigned char* rgb = image.GetData();
        for (size_t i = 0; i < chars.size(); ++i) {
            auto gb = std::make_shared<GlyphBitmap>();
            const int pixelX = wxRound(slots[i].x * m_rasterScale);
            const int pixelW = wxRound((slots[i].x + slots[i].w) * m_rasterScale) - pixelX;
            const int pixelH = wxRound(slots[i].h * m_rasterScale);
            int left = pixelW, top = pixelH, right = -1, bottom = -1;
            for (int y = 0; y < pixelH; ++y) {
                for (int x = 0; x < pixelW; ++x) {
                    const auto* p = rgb + ((size_t)y * image.GetWidth() + pixelX + x) * 3;
                    if ((p[0] + p[1] + p[2]) / 3 == 0) continue;
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            }
            gb->advance = slots[i].advance;
            if (right < left) { // Space or another glyph without visible ink
                gb->width = gb->height = 1;
                gb->rgba = {255, 255, 255, 0};
                out.emplace_back(chars[i].first, gb);
                continue;
            }
            gb->width = right - left + 1;
            gb->height = bottom - top + 1;
            gb->bearingX = left / m_rasterScale - padding;
            gb->bearingY = slots[i].top + top / m_rasterScale;
            gb->rgba.resize(gb->width * gb->height * 4);
            ExtractRGBA(rgb + ((size_t)top * image.GetWidth() + pixelX + left) * 3,
                        (size_t)image.GetWidth() * 3,
                        gb->rgba.data(), gb->width, gb->height);
            out.emplace_back(chars[i].first, gb);
        }
        return out;
    }

private:
    wxFont m_font;
    int m_charHeight;
    int m_baseline;
    float m_rasterScale;
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
    for (int s = 0; s < 4; ++s) {
        for (int i = 0; i < 128; ++i) {
            m_asciiMetricsCached[s][i] = false;
        }
    }
}

FontAtlas::~FontAtlas() {
    if (m_textureID != 0) {
        glDeleteTextures(1, &m_textureID);
    }
}

bool FontAtlas::InitializeSystemFont(int fontSize, const wxString& fontName, float rasterScale) {
    InitSSHLogFile();
    SSH_LOG("FontAtlas::InitializeSystemFont called with fontSize=" << fontSize << ", fontName='" << fontName << "'");

    m_fontSize = fontSize;
    m_rasterScale = rasterScale;

    // Create the four style variants up front: index = bold | italic<<1,
    // matching the style bits used by CellInstance::attrs.
    for (int s = 0; s < 4; ++s) {
        const auto fstyle = (s & 2) ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL;
        const auto fweight = (s & 1) ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL;
        if (fontName.IsEmpty()) {
            m_fonts[s] = wxFont(fontSize, wxFONTFAMILY_MODERN, fstyle, fweight);
        } else {
            m_fonts[s] = wxFont(fontSize, wxFONTFAMILY_MODERN, fstyle, fweight, false, fontName);
        }
        if (!m_fonts[s].IsOk()) {
            m_fonts[s] = wxFont(fontSize, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
        }
    }
    if (!m_fonts[0].IsOk()) {
        SSH_LOG("FontAtlas: Failed to create font with name '" << fontName << "' and size " << fontSize);
        return false;
    }

    // Calculate actual character height including descenders
    // Use a temporary DC to get font metrics
    wxBitmap tempBitmap(1, 1, 24);
    wxMemoryDC tempDC(tempBitmap);
    wxGCDC gcdc(tempDC); // Use GCDC for accurate font metrics
    gcdc.SetFont(m_fonts[0]);

    // Get text extent for a string containing ascenders and descenders
    wxString testChar = "Mg|jpqy_";
    int textWidth, textHeight, descent;
    gcdc.GetTextExtent(testChar, &textWidth, &textHeight, &descent);

    // Use the actual character height including descenders
    m_charHeight = textHeight;
    m_baseline = textHeight - descent;

#ifdef _WIN32
    // On Windows, add padding to character height to provide space for underline rendering
    m_charHeight += 4;
#endif

    tempDC.SelectObject(wxNullBitmap);

    // Ensure minimum height
    if (m_charHeight < fontSize) {
        m_charHeight = fontSize;
    }

    const std::string faceKey = m_fonts[0].GetFaceName().ToStdString() + "#"
        + std::to_string(m_fontSize) + "#" + std::to_string(m_rasterScale);
    for (int s = 0; s < 4; ++s) {
        m_cacheKeys[s] = faceKey + "#" + std::to_string(s);
    }

    SSH_LOG("FontAtlas: Font created successfully, name='" << m_fonts[0].GetFaceName() << "', size=" << fontSize << ", charHeight=" << m_charHeight << ", rasterScale=" << m_rasterScale);
    return GenerateTextureAtlas();
}

bool FontAtlas::GenerateTextureAtlas() {
    int charSize = m_fontSize;
    
    m_textureWidth = wxRound(1024 * m_rasterScale);
    m_textureHeight = wxRound(1024 * m_rasterScale);
    
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
    m_freeSpaces.emplace_back(0, 0, m_textureWidth, m_textureHeight);
    
    // Pre-load ASCII characters for every style variant: rasterize in one
    // pass per style and share the bitmaps process-wide; later atlases only
    // upload from the cache.
    for (int style = 0; style < 4; ++style) {
        std::vector<std::pair<char32_t, wxString>> missing;
        {
            std::lock_guard<std::mutex> lock(s_glyphMutex);
            auto& byChar = s_glyphCache[m_cacheKeys[style]];
            for (char32_t c = 32; c <= 126; ++c) {
                if (!byChar.count(c)) missing.emplace_back(c, CharToWxString(c));
            }
        }
        if (!missing.empty()) {
            GlyphRasterizer rasterizer(m_fonts[style], m_charHeight, m_baseline, m_rasterScale);
            auto glyphs = rasterizer.RasterizeBatch(missing);
            std::lock_guard<std::mutex> lock(s_glyphMutex);
            auto& byChar = s_glyphCache[m_cacheKeys[style]];
            for (auto& g : glyphs) {
                byChar[g.first] = std::move(g.second);
            }
        }
        for (char32_t c = 32; c <= 126; ++c) {
            std::shared_ptr<GlyphBitmap> gb;
            {
                std::lock_guard<std::mutex> lock(s_glyphMutex);
                auto it = s_glyphCache[m_cacheKeys[style]].find(c);
                if (it != s_glyphCache[m_cacheKeys[style]].end()) gb = it->second;
            }
            if (gb) {
                UploadGlyph(c, style, gb->width, gb->height, gb->advance, gb->bearingX, gb->bearingY, gb->rgba.data());
            }
        }
    }
    
    SSH_LOG("FontAtlas: Pre-loaded ASCII characters");
    return true;
}

bool FontAtlas::AddCharToAtlas(char32_t charCode, int style) {
    style &= 3;
    // Check if character already exists
    auto it = m_charMetrics[style].find(charCode);
    if (it != m_charMetrics[style].end()) {
        it->second.last_used = m_currentTime++;
        return true;
    }

    std::shared_ptr<GlyphBitmap> glyph;
    {
        std::lock_guard<std::mutex> lock(s_glyphMutex);
        auto& byChar = s_glyphCache[m_cacheKeys[style]];
        auto git = byChar.find(charCode);
        if (git != byChar.end()) glyph = git->second;
    }
    if (!glyph) {
        GlyphRasterizer rasterizer(m_fonts[style], m_charHeight, m_baseline, m_rasterScale);
        glyph = std::make_shared<GlyphBitmap>(rasterizer.Rasterize(CharToWxString(charCode)));
        std::lock_guard<std::mutex> lock(s_glyphMutex);
        auto& byChar = s_glyphCache[m_cacheKeys[style]];
        if (byChar.size() < kGlyphCacheLimitPerFont) {
            byChar[charCode] = glyph;
        }
    }

    return UploadGlyph(charCode, style, glyph->width, glyph->height,
                       glyph->advance, glyph->bearingX, glyph->bearingY, glyph->rgba.data());
}

bool FontAtlas::UploadGlyph(char32_t charCode, int style, int bitmapWidth, int bitmapHeight,
                            int advance, float bearingX, float bearingY, const unsigned char* rgba) {
    if (bitmapWidth <= 0 || bitmapHeight <= 0 || !rgba) return false;

    // Find free space in atlas
    int x, y;
    if (!FindFreeSpace(bitmapWidth, bitmapHeight, x, y)) {
        // Reuse an old slot large enough for this glyph's actual ink bounds.
        EvictLRU(bitmapWidth, bitmapHeight);
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
    metrics.bearingY = bearingY;
    metrics.actual_width = static_cast<float>(bitmapWidth) / m_rasterScale;
    metrics.actual_height = static_cast<float>(bitmapHeight) / m_rasterScale;
    metrics.last_used = m_currentTime++;
    metrics.tex_x = x;
    metrics.tex_y = y;
    metrics.tex_w = bitmapWidth;
    metrics.tex_h = bitmapHeight;

    m_charMetrics[style & 3][charCode] = metrics;
    return true;
}

bool FontAtlas::FindFreeSpace(int charWidth, int charHeight, int& outX, int& outY) {
    // Cropped glyphs have different heights. An origin alone is insufficient
    // to describe reusable space: a taller/wider glyph could overwrite a neighbour.
    auto best = m_freeSpaces.end();
    for (auto it = m_freeSpaces.begin(); it != m_freeSpaces.end(); ++it) {
        if (it->width >= charWidth && it->height >= charHeight &&
            (best == m_freeSpaces.end() ||
             it->width * it->height < best->width * best->height)) {
            best = it;
        }
    }
    if (best == m_freeSpaces.end()) return false;
    const wxRect free = *best;
    m_freeSpaces.erase(best);
    outX = free.x;
    outY = free.y;
    if (free.width > charWidth)
        m_freeSpaces.emplace_back(free.x + charWidth, free.y,
                                  free.width - charWidth, charHeight);
    if (free.height > charHeight)
        m_freeSpaces.emplace_back(free.x, free.y + charHeight,
                                  free.width, free.height - charHeight);
    return true;
}

void FontAtlas::EvictLRU(int requiredWidth, int requiredHeight) {
    char32_t oldestChar = 0;
    int oldestStyle = 0;
    int oldestTime = m_currentTime;
    
    for (int s = 0; s < 4; ++s) {
        for (const auto& [charCode, metrics] : m_charMetrics[s]) {
            if (metrics.tex_w >= requiredWidth && metrics.tex_h >= requiredHeight &&
                metrics.last_used < oldestTime) {
                oldestTime = metrics.last_used;
                oldestChar = charCode;
                oldestStyle = s;
            }
        }
    }
    
    if (oldestChar != 0) {
        SSH_LOG("FontAtlas: Evicting LRU character 0x" << std::hex << static_cast<uint32_t>(oldestChar) << std::dec);
        const auto& metrics = m_charMetrics[oldestStyle][oldestChar];
        
        int totalPixels = metrics.tex_w * metrics.tex_h * 4;
        unsigned char* clearData = new unsigned char[totalPixels];
        memset(clearData, 0, totalPixels);
        UpdateTexture(metrics.tex_x, metrics.tex_y, metrics.tex_w, metrics.tex_h, clearData);
        delete[] clearData;
        
        m_freeSpaces.emplace_back(metrics.tex_x, metrics.tex_y, metrics.tex_w, metrics.tex_h);
        if (oldestChar < 128) m_asciiMetricsCached[oldestStyle][oldestChar] = false;
        
        m_charMetrics[oldestStyle].erase(oldestChar);
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

CharMetrics FontAtlas::GetCharMetrics(char32_t charCode, int style) {
    style &= 3;
    if (charCode < 128) {
        if (m_asciiMetricsCached[style][charCode]) {
            return m_asciiMetrics[style][charCode];
        }
    }

    auto it = m_charMetrics[style].find(charCode);
    if (it != m_charMetrics[style].end()) {
        it->second.last_used = m_currentTime++;
        CharMetrics metrics = it->second;
        if (charCode < 128) {
            m_asciiMetrics[style][charCode] = metrics;
            m_asciiMetricsCached[style][charCode] = true;
        }
        return metrics;
    }
    
    // Character not found, try to add it dynamically
    if (AddCharToAtlas(charCode, style)) {
        it = m_charMetrics[style].find(charCode);
        if (it != m_charMetrics[style].end()) {
            CharMetrics metrics = it->second;
            if (charCode < 128) {
                m_asciiMetrics[style][charCode] = metrics;
                m_asciiMetricsCached[style][charCode] = true;
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
    defaultMetrics.actual_width = static_cast<float>(m_fontSize);
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
