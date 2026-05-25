#include "DeviceConfig.h"
#include "GlobalConfig.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <vector>
#include <cstdint>

using json = nlohmann::json;

static wxString GetConfigPath() {
    wxFileName configPath(wxString::FromUTF8(GlobalConfig::GetWorkspacePath()), "");
    configPath.AppendDir("config");
    configPath.SetFullName("oc.json");
    return configPath.GetFullPath();
}

static wxString GetConfigDir() {
    wxFileName configDir(wxString::FromUTF8(GlobalConfig::GetWorkspacePath()), "");
    configDir.AppendDir("config");
    return configDir.GetPath();
}

static std::string base64_decode(const std::string& encoded) {
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string decoded;
    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (c == '=') break;
        if (chars.find(c) == std::string::npos) continue;
        val = (val << 6) + chars.find(c);
        valb += 6;
        if (valb >= 0) {
            decoded.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return decoded;
}

std::vector<DeviceConfig> DeviceConfig::LoadFromFile() {
    std::vector<DeviceConfig> devices;
    wxString configPath = GetConfigPath();

    std::ifstream file(configPath.ToStdString());
    if (!file.is_open()) {
        return devices;
    }
    try {
        json data = json::parse(file);
        for (const auto& item : data["root"]) {
            DeviceConfig dev;
            dev.id = item.value("id", "");
            dev.name = item.value("name", "");
            dev.username = item.value("username", "");
            dev.group = item.value("group", "");
            dev.address = item.value("address", "");
            dev.port = item.value("port", "22");
            dev.auth_method = item.value("authType", "password");
            dev.password = item.value("password", "");
            devices.push_back(dev);
        }
    } catch (const std::exception& e) {
    }
    return devices;
}

void DeviceConfig::SaveToFile(const std::vector<DeviceConfig>& devices) {
    json data;
    data["root"] = json::array();
    for (const auto& dev : devices) {
        json item;
        item["id"] = dev.id;
        item["name"] = dev.name;
        item["username"] = dev.username;
        item["group"] = dev.group;
        item["address"] = dev.address;
        item["port"] = dev.port;
        item["authType"] = dev.auth_method;
        item["password"] = dev.password;
        data["root"].push_back(item);
    }

    wxString configDir = GetConfigDir();
    wxString configPath = GetConfigPath();

    if (!wxDirExists(configDir)) {
        wxFileName::Mkdir(configDir);
    }

    std::ofstream file(configPath.ToStdString());
    if (file.is_open()) {
        file << data.dump(2);
        file.close();
    }
}
