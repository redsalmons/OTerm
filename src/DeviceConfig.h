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

    static std::vector<DeviceConfig> LoadFromFile();
    static void SaveToFile(const std::vector<DeviceConfig>& devices);

    static bool ReencryptWithNewPassword(const std::string& oldPassword, const std::string& newPassword);
};
