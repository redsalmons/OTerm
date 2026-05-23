#ifndef GLOBALCONFIG_H
#define GLOBALCONFIG_H

#include <wx/wx.h>
#include <string>

class GlobalConfig {
public:
    static std::string GetWorkspacePath();
    static void SetWorkspacePath(const std::string& path);
    static void InitializeFromCommandLine(int argc, char** argv);
    
    static std::string GetLanguage();
    static void SetLanguage(const std::string& language);
    
    static std::string GetFontName();
    static void SetFontName(const std::string& fontName);
    
    static int GetFontSize();
    static void SetFontSize(int fontSize);
    
    static void LoadSettings();
    static void SaveSettings();

private:
    static std::string s_workspacePath;
    static std::string s_language;
    static std::string s_fontName;
    static int s_fontSize;
    
    static std::string GetSettingsFilePath();
};

#endif
