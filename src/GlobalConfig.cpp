#include "GlobalConfig.h"
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::string GlobalConfig::s_workspacePath;
std::string GlobalConfig::s_language = "zh_CN";
std::string GlobalConfig::s_fontName = "";
int GlobalConfig::s_fontSize = 0;

std::string GlobalConfig::GetWorkspacePath() {
    if (s_workspacePath.empty()) {
        wxStandardPaths& stdPaths = wxStandardPaths::Get();
        wxString exePath = stdPaths.GetExecutablePath();
        wxFileName fn(exePath);
        wxFileName workspaceDir(fn.GetPath(), "");
        workspaceDir.RemoveLastDir();
        s_workspacePath = workspaceDir.GetPath().ToStdString();
    }
    return s_workspacePath;
}

void GlobalConfig::SetWorkspacePath(const std::string& path) {
    s_workspacePath = path;
}

void GlobalConfig::InitializeFromCommandLine(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-p" && i + 1 < argc) {
            s_workspacePath = argv[i + 1];
            i++; // Skip the next argument
            break;
        }
    }
    
    // Load settings after workspace path is set
    LoadSettings();
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

std::string GlobalConfig::GetSettingsFilePath() {
    std::string workspace = GetWorkspacePath();
    return workspace + "/settings.json";
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
    } catch (const std::exception& e) {
        // Error parsing JSON, use default values
        s_language = "zh_CN";
        s_fontName = "";
        s_fontSize = 0;
    }
}

void GlobalConfig::SaveSettings() {
    std::string settingsPath = GetSettingsFilePath();
    
    json j;
    j["language"] = s_language;
    j["fontName"] = s_fontName;
    j["fontSize"] = s_fontSize;
    
    std::ofstream file(settingsPath);
    if (file.is_open()) {
        file << j.dump(4);
    }
}
