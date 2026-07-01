#include "GlobalConfig.h"
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <openssl/md5.h>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;

std::string GlobalConfig::s_workspacePath;
bool GlobalConfig::s_workspacePathSpecified = false;
std::string GlobalConfig::s_language = "zh_CN";
std::string GlobalConfig::s_fontName = "";
int GlobalConfig::s_fontSize = 0;
std::string GlobalConfig::s_masterPassword = "";
std::string GlobalConfig::x7f3a9k2m5p8q1r4 = "";
double GlobalConfig::s_dpiScaleFactor = 1.0;

std::string GlobalConfig::GetWorkspacePath() {
    if (s_workspacePath.empty()) {
        wxStandardPaths& stdPaths = wxStandardPaths::Get();
        
#ifdef __WXMAC__
        // On macOS, use user data directory for app bundles
        wxString exePath = stdPaths.GetExecutablePath();
        if (exePath.Contains(".app/Contents/MacOS/")) {
            // Running from app bundle, use user data directory
            wxString userDataDir = stdPaths.GetUserDataDir();
            s_workspacePath = userDataDir.ToStdString();
        } else {
            // Not running from app bundle, use executable directory
            wxFileName fn(exePath);
            wxFileName workspaceDir(fn.GetPath(), "");
            workspaceDir.RemoveLastDir();
            s_workspacePath = workspaceDir.GetPath().ToStdString();
        }
#elif defined(_WIN32)
        // On Windows, use user data directory for installed applications
        wxString userDataDir = stdPaths.GetUserDataDir();
        s_workspacePath = userDataDir.ToStdString();
#else
        // Other platforms (Linux), use executable directory
        wxString exePath = stdPaths.GetExecutablePath();
        wxFileName fn(exePath);
        wxFileName workspaceDir(fn.GetPath(), "");
        workspaceDir.RemoveLastDir();
        s_workspacePath = workspaceDir.GetPath().ToStdString();
#endif
    }
    return s_workspacePath;
}

void GlobalConfig::SetWorkspacePath(const std::string& path) {
    s_workspacePath = path;
    s_workspacePathSpecified = true;
}

void GlobalConfig::InitializeFromCommandLine(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-p" && i + 1 < argc) {
            s_workspacePath = argv[i + 1];
            s_workspacePathSpecified = true;
            i++; // Skip the next argument
            break;
        }
    }

    // Load settings after workspace path is set
    LoadSettings();
}

std::string GlobalConfig::GetLogPath() {
    if (s_workspacePathSpecified) {
        // If workspace path is specified, logs go to workspace/logs
        return s_workspacePath + "/logs";
    } else {
        // Otherwise, logs go to user local data directory
        wxStandardPaths& stdPaths = wxStandardPaths::Get();
        return stdPaths.GetUserLocalDataDir().ToStdString();
    }
}

std::string GlobalConfig::GetConfigPath() {
    if (s_workspacePathSpecified) {
        // If workspace path is specified, config goes to workspace/config
        return s_workspacePath + "/config";
    } else {
        // Otherwise, config goes to user config directory (Preferences)
        wxStandardPaths& stdPaths = wxStandardPaths::Get();
        wxString configDir = stdPaths.GetUserConfigDir();
        // Add app name as subdirectory
        configDir += "/OceanTerm";
        return configDir.ToStdString();
    }
}

std::string GlobalConfig::GetLanguage() {
    return s_language;
}

void GlobalConfig::SetLanguage(const std::string& language) {
    s_language = language;
}

std::string GlobalConfig::GetFontName() {
    if (s_fontName.empty()) {
        // Get system default font
        wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        s_fontName = font.GetFaceName().ToStdString();
    }
    return s_fontName;
}

void GlobalConfig::SetFontName(const std::string& fontName) {
    s_fontName = fontName;
}

int GlobalConfig::GetFontSize() {
    if (s_fontSize == 0) {
        // Get system default font size
        wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        s_fontSize = font.GetPointSize();
    }
    return s_fontSize;
}

void GlobalConfig::SetFontSize(int fontSize) {
    s_fontSize = fontSize;
}

std::string GlobalConfig::GetMasterPassword() {
    return s_masterPassword;
}

void GlobalConfig::SetMasterPassword(const std::string& password) {
    s_masterPassword = ComputeMD5(password);
}

bool GlobalConfig::HasMasterPassword() {
    return !s_masterPassword.empty();
}

bool GlobalConfig::VerifyMasterPassword(const std::string& password) {
    std::string passwordHash = ComputeMD5(password);
    return passwordHash == s_masterPassword;
}

std::string GlobalConfig::GetActiveMasterPassword() {
    return x7f3a9k2m5p8q1r4;
}

void GlobalConfig::SetActiveMasterPassword(const std::string& password) {
    x7f3a9k2m5p8q1r4 = password;
}

std::string GlobalConfig::ComputeMD5(const std::string& input) {
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5((unsigned char*)input.c_str(), input.size(), digest);

    std::stringstream ss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    }
    return ss.str();
}

std::string GlobalConfig::GetSettingsFilePath() {
    std::string configPath = GetConfigPath();
    return configPath + "/settings.json";
}

void GlobalConfig::LoadSettings() {
    std::string settingsPath = GetSettingsFilePath();
    std::ifstream file(settingsPath);
    
    if (!file.is_open()) {
        // File doesn't exist, use default values
        s_language = "zh_CN";
        s_fontName = "";
        s_fontSize = 0;
        return;
    }
    
    try {
        json j;
        file >> j;
        
        if (j.contains("language")) {
            s_language = j["language"].get<std::string>();
        } else {
            s_language = "zh_CN"; // Default
        }
        
        if (j.contains("fontName")) {
            s_fontName = j["fontName"].get<std::string>();
        } else {
            s_fontName = ""; // Default to system font
        }
        
        if (j.contains("fontSize")) {
            s_fontSize = j["fontSize"].get<int>();
        } else {
            s_fontSize = 0; // Default to system font size
        }

        if (j.contains("masterPassword")) {
            s_masterPassword = j["masterPassword"].get<std::string>();
        } else {
            s_masterPassword = ""; // Default no master password
        }
    } catch (const std::exception& e) {
        // Error parsing JSON, use default values
        s_language = "zh_CN";
        s_fontName = "";
        s_fontSize = 0;
    }
}

void GlobalConfig::SaveSettings() {
    std::string settingsPath = GetSettingsFilePath();

    // Ensure directory exists
    std::string configDir = GetConfigPath();
    wxFileName::Mkdir(wxString::FromUTF8(configDir.c_str()), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    json j;
    j["language"] = s_language;
    j["fontName"] = s_fontName;
    j["fontSize"] = s_fontSize;
    j["masterPassword"] = s_masterPassword;

    std::ofstream file(settingsPath);
    if (file.is_open()) {
        file << j.dump(4);
        file.close();
    }
}

void GlobalConfig::SetDPIScaleFactor(double scale) {
    s_dpiScaleFactor = scale;
}

double GlobalConfig::GetDPIScaleFactor() {
    if (s_dpiScaleFactor <= 0.0) {
        s_dpiScaleFactor = 1.0;
    }
    return s_dpiScaleFactor;
}

int GlobalConfig::GetTerminalFontSize(double dpiScale) {
    int fontSize = GetFontSize();
    if (fontSize <= 0) fontSize = 12;
    
    // Apply DPI scaling
    if (dpiScale > 1.0f) {
        fontSize = static_cast<int>(fontSize * 2);
    }
    
    // Clamp to valid range
    if (fontSize < 8) fontSize = 8;
    if (fontSize > 72) fontSize = 72;
    
    return fontSize;
}
