#include "GlobalConfig.h"
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::string GlobalConfig::s_workspacePath;
std::string GlobalConfig::s_language = "zh_CN";

std::string GlobalConfig::GetWorkspacePath() {
    if (s_workspacePath.empty()) {
        // Default to program directory if not set
        wxStandardPaths& stdPaths = wxStandardPaths::Get();
        wxString exePath = stdPaths.GetExecutablePath();
        wxFileName fn(exePath);
        wxString dir = fn.GetPath();
        s_workspacePath = dir.ToStdString();
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
    } catch (const std::exception& e) {
        // Error parsing JSON, use default values
        s_language = "zh_CN";
    }
}

void GlobalConfig::SaveSettings() {
    std::string settingsPath = GetSettingsFilePath();
    
    json j;
    j["language"] = s_language;
    
    std::ofstream file(settingsPath);
    if (file.is_open()) {
        file << j.dump(4);
    }
}
