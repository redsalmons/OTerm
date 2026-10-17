#pragma once

#include <string>
#include <vector>

struct DeviceConfig {
    std::string id;
    std::string name;
    std::string username;
    std::string address;
    std::string port;
    std::string group;
    std::string auth_method = "password";
    std::string password;
    std::string key;

    static std::vector<DeviceConfig> LoadFromFile();
    static void SaveToFile(const std::vector<DeviceConfig>& devices);

    static bool ReencryptWithNewPassword(const std::string& oldPassword, const std::string& newPassword);

    // Remove a stored credential (Keychain on macOS; no-op elsewhere)
    static void DeleteCredential(const std::string& deviceId);

    // Generic credential storage (Keychain on macOS)
    static bool StoreCredential(const std::string& key, const std::string& secret);
    static std::string LoadCredential(const std::string& key);
};
