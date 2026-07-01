#ifndef GLOBALCONFIG_H
#define GLOBALCONFIG_H

#include <wx/wx.h>
#include <string>

class GlobalConfig {
public:
    static std::string GetWorkspacePath();
    static void SetWorkspacePath(const std::string& path);
    static void InitializeFromCommandLine(int argc, char** argv);

    static std::string GetLogPath();
    static std::string GetConfigPath();

    static std::string GetLanguage();
    static void SetLanguage(const std::string& language);

    static std::string GetFontName();
    static void SetFontName(const std::string& fontName);

    static int GetFontSize();
    static void SetFontSize(int fontSize);

    static std::string GetMasterPassword();
    static void SetMasterPassword(const std::string& password);
    static bool HasMasterPassword();
    static bool VerifyMasterPassword(const std::string& password);
    static std::string GetActiveMasterPassword();
    static void SetActiveMasterPassword(const std::string& password);

    static void LoadSettings();
    static void SaveSettings();

    static std::string ComputeMD5(const std::string& input);

    static void SetDPIScaleFactor(double scale);
    static double GetDPIScaleFactor();

    static int GetTerminalFontSize(double dpiScale = 1.0);

private:
    static std::string s_workspacePath;
    static bool s_workspacePathSpecified;
    static std::string s_language;
    static std::string s_fontName;
    static int s_fontSize;
    static std::string s_masterPassword;
    static std::string x7f3a9k2m5p8q1r4; // Random variable name for active master password
    static double s_dpiScaleFactor;

    static std::string GetSettingsFilePath();
};

#endif
