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
};

class FontAtlas {
public:
    FontAtlas();
    ~FontAtlas();
    
    bool Initialize(const wxString& fontPath, int fontSize);
    bool InitializeSystemFont(int fontSize);
    
    CharMetrics GetCharMetrics(char32_t charCode) const;
    unsigned int GetTextureID() const;
    int GetTextureWidth() const;
    int GetTextureHeight() const;
    int GetFontSize() const;
    
private:
    bool GenerateTextureAtlas();
    void AddCharToAtlas(char32_t charCode, wxDC& dc, int& x, int& y, int rowHeight);
    
    unsigned int m_textureID;
    int m_textureWidth;
    int m_textureHeight;
    int m_fontSize;
    wxFont m_font;
    
    std::unordered_map<char32_t, CharMetrics> m_charMetrics;
};

#endif // FONT_ATLAS_H
