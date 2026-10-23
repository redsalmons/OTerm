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

    // Remove a stored credential (Keychain on macOS; no-op elsewhere)
    static void DeleteCredential(const std::string& deviceId);

    // Generic credential storage (Keychain on macOS)
    static bool StoreCredential(const std::string& key, const std::string& secret);
    static std::string LoadCredential(const std::string& key);
    // False if the credential store exists but could not be read (e.g. the
    // user denied Keychain access); callers must not regenerate secrets then.
    static bool IsCredentialStoreAvailable();
};
