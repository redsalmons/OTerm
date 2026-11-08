#ifndef FONT_ATLAS_H
#define FONT_ATLAS_H

#include <wx/wx.h>
#include <unordered_map>
#include <string>

struct CharMetrics {
    float u, v;      // UV coordinates in texture atlas
    float w, h;      // Width and height in texture coordinates
    float advance;   // Advance to next character
    float bearingX;  // Bitmap left relative to the cell origin, in terminal units
    float bearingY;  // Bitmap top relative to the cell origin, in terminal units
    float actual_width;  // Bitmap dimensions in terminal units, without stretching
    float actual_height;
    int last_used;   // Timestamp for LRU cache
    int tex_x, tex_y; // Texture coordinates for eviction
    int tex_w, tex_h; // Texture dimensions for eviction
};

class FontAtlas {
public:
    FontAtlas();
    ~FontAtlas();
    
    bool Initialize(const wxString& fontPath, int fontSize);
    bool InitializeSystemFont(int fontSize, const wxString& fontName = "", float rasterScale = 1.0f);
    
    // style: bit0 = bold, bit1 = italic (same bits as CellInstance::attrs)
    CharMetrics GetCharMetrics(char32_t charCode, int style = 0);
    unsigned int GetTextureID() const;
    int GetTextureWidth() const;
    int GetTextureHeight() const;
    int GetFontSize() const;
    int GetCharHeight() const; // Get actual character rendering height
    int GetBaseline() const { return m_baseline; }
    float GetRasterScale() const { return m_rasterScale; }
    
private:
    bool GenerateTextureAtlas();
    bool AddCharToAtlas(char32_t charCode, int style);
    bool FindFreeSpace(int charWidth, int charHeight, int& outX, int& outY);
    void EvictLRU(int requiredWidth, int requiredHeight);
    void UpdateTexture(int x, int y, int width, int height, const unsigned char* data);
    bool UploadGlyph(char32_t charCode, int style, int bitmapWidth, int bitmapHeight,
                     int advance, float bearingX, float bearingY, const unsigned char* rgba);
    
    unsigned int m_textureID;
    int m_textureWidth;
    int m_textureHeight;
    int m_fontSize;
    int m_charHeight; // Actual character rendering height
    int m_baseline = 0; // Distance from the cell top to the shared text baseline
    float m_rasterScale = 1.0f; // Bitmap pixels per terminal coordinate unit
    wxFont m_fonts[4]; // normal, bold, italic, bold-italic
    std::string m_cacheKeys[4]; // "face#size#scale#style" for the process-wide glyph cache
    
    std::unordered_map<char32_t, CharMetrics> m_charMetrics[4];
    CharMetrics m_asciiMetrics[4][128];
    bool m_asciiMetricsCached[4][128];
    std::vector<wxRect> m_freeSpaces; // Sized free rectangles, including evicted glyphs
    int m_currentTime; // Global timestamp for LRU
};

#endif // FONT_ATLAS_H
