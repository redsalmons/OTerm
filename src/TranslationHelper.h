#ifndef TRANSLATIONHELPER_H
#define TRANSLATIONHELPER_H

#include <wx/wx.h>
#include <map>
#include <string>

class TranslationHelper {
public:
    static void Load(const wxString& language);
    static wxString Translate(const wxString& text);
    static wxString Tr(const wxString& text);

private:
    static std::map<wxString, wxString> s_translations;
    static void LoadPOFile(const wxString& poFilePath);
};

#endif
