#ifndef FONT_ATLAS_H
#define FONT_ATLAS_H

#include <wx/wx.h>
#include <unordered_map>

struct CharMetrics {
    float u, v;      // UV coordinates in texture atlas
    float w, h;      // Width and height in texture coordinates
    float advance;   // Advance to next character
    float bearingX;  // X bearing
    float bearingY;  // Y bearing
    float actual_height; // Actual character height (for rendering)
    int last_used;   // Timestamp for LRU cache
    int tex_x, tex_y; // Texture coordinates for eviction
    int tex_w, tex_h; // Texture dimensions for eviction
};

class FontAtlas {
public:
    FontAtlas();
    ~FontAtlas();
    
    bool Initialize(const wxString& fontPath, int fontSize);
    bool InitializeSystemFont(int fontSize, const wxString& fontName = "");
    
    CharMetrics GetCharMetrics(char32_t charCode);
    unsigned int GetTextureID() const;
    int GetTextureWidth() const;
    int GetTextureHeight() const;
    int GetFontSize() const;
    int GetCharHeight() const; // Get actual character rendering height
    
private:
    bool GenerateTextureAtlas();
    bool AddCharToAtlas(char32_t charCode);
    bool FindFreeSpace(int charWidth, int charHeight, int& outX, int& outY);
    void EvictLRU();
    void UpdateTexture(int x, int y, int width, int height, unsigned char* data);
    
    unsigned int m_textureID;
    int m_textureWidth;
    int m_textureHeight;
    int m_fontSize;
    int m_charHeight; // Actual character rendering height
    wxFont m_font;
    
    std::unordered_map<char32_t, CharMetrics> m_charMetrics;
    CharMetrics m_asciiMetrics[128];
    bool m_asciiMetricsCached[128];
    std::vector<std::pair<int, int>> m_freeSpaces; // Available spaces in atlas
    int m_currentTime; // Global timestamp for LRU
};

#endif // FONT_ATLAS_H
