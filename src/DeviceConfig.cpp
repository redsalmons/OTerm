#include "DeviceConfig.h"
#include "GlobalConfig.h"
#include "SSHManager.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <vector>
#include <cstdint>
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <cstring>
#include <random>

using json = nlohmann::json;

static wxString GetConfigPath() {
    wxFileName configPath(wxString::FromUTF8(GlobalConfig::GetConfigPath()), "");
    configPath.SetFullName("oc.json");
    return configPath.GetFullPath();
}

static wxString GetConfigDir() {
    return wxString::FromUTF8(GlobalConfig::GetConfigPath());
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

static std::string base64_encode(const std::string& input) {
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            encoded.push_back(chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        encoded.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (encoded.size() % 4) {
        encoded.push_back('=');
    }
    return encoded;
}

static std::string derive_key(const std::string& password, const unsigned char* salt) {
    // Simple key derivation using MD5
    std::string salted = password + std::string((char*)salt, 8);
    return GlobalConfig::ComputeMD5(salted);
}

static std::string encrypt_aes(const std::string& plaintext, const std::string& password) {
    // Generate random salt
    unsigned char salt[8];
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (int i = 0; i < 8; i++) {
        salt[i] = static_cast<unsigned char>(dis(gen));
    }

    // Derive key
    std::string key = derive_key(password, salt);

    // Generate random IV
    unsigned char iv[AES_BLOCK_SIZE];
    for (int i = 0; i < AES_BLOCK_SIZE; i++) {
        iv[i] = static_cast<unsigned char>(dis(gen));
    }

    // Encrypt
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return "";

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, (unsigned char*)key.c_str(), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    std::string ciphertext;
    ciphertext.resize(plaintext.size() + AES_BLOCK_SIZE);
    int len;
    int ciphertext_len;

    if (EVP_EncryptUpdate(ctx, (unsigned char*)&ciphertext[0], &len, (unsigned char*)plaintext.c_str(), plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    ciphertext_len = len;

    if (EVP_EncryptFinal_ex(ctx, (unsigned char*)&ciphertext[len], &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    ciphertext_len += len;

    ciphertext.resize(ciphertext_len);
    EVP_CIPHER_CTX_free(ctx);

    // Combine salt + iv + ciphertext
    std::string result;
    result.append((char*)salt, 8);
    result.append((char*)iv, AES_BLOCK_SIZE);
    result.append(ciphertext);

    return base64_encode(result);
}

static std::string decrypt_aes(const std::string& ciphertext_b64, const std::string& password) {
    std::string ciphertext = base64_decode(ciphertext_b64);

    if (ciphertext.size() < 8 + AES_BLOCK_SIZE) {
        return "";
    }

    // Extract salt and IV
    unsigned char salt[8];
    unsigned char iv[AES_BLOCK_SIZE];
    memcpy(salt, ciphertext.data(), 8);
    memcpy(iv, ciphertext.data() + 8, AES_BLOCK_SIZE);

    std::string actual_ciphertext = ciphertext.substr(8 + AES_BLOCK_SIZE);

    // Derive key
    std::string key = derive_key(password, salt);

    // Decrypt
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return "";

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, (unsigned char*)key.c_str(), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    std::string plaintext;
    plaintext.resize(actual_ciphertext.size() + AES_BLOCK_SIZE);
    int len;
    int plaintext_len;

    if (EVP_DecryptUpdate(ctx, (unsigned char*)&plaintext[0], &len, (unsigned char*)actual_ciphertext.c_str(), actual_ciphertext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    plaintext_len = len;

    if (EVP_DecryptFinal_ex(ctx, (unsigned char*)&plaintext[len], &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    plaintext_len += len;

    plaintext.resize(plaintext_len);
    EVP_CIPHER_CTX_free(ctx);

    return plaintext;
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

        // Get master password for decryption
        std::string masterPassword = GlobalConfig::GetActiveMasterPassword();

        for (const auto& item : data["root"]) {
            DeviceConfig dev;
            dev.id = item.value("id", "");
            dev.name = item.value("name", "");
            dev.group = item.value("group", "");
            
            // Handle both nested "config" structure and flat structure
            if (item.contains("config") && item["config"].is_object()) {
                auto& config = item["config"];
                dev.username = config.value("username", "");
                dev.address = config.value("address", "");
                dev.port = config.value("port", "22");
                dev.auth_method = config.value("authMethod", "password");
                std::string encryptedPassword = config.value("password", "");
                if (!encryptedPassword.empty() && !masterPassword.empty()) {
                    dev.password = decrypt_aes(encryptedPassword, masterPassword);
                } else {
                    dev.password = encryptedPassword;
                }
            } else {
                // Flat structure (fallback)
                dev.username = item.value("username", "");
                dev.address = item.value("address", "");
                dev.port = item.value("port", "22");
                dev.auth_method = item.value("authType", "password");
                std::string encryptedPassword = item.value("password", "");
                if (!encryptedPassword.empty() && !masterPassword.empty()) {
                    dev.password = decrypt_aes(encryptedPassword, masterPassword);
                } else {
                    dev.password = encryptedPassword;
                }
            }

            devices.push_back(dev);
        }
    } catch (const std::exception& e) {
    }
    return devices;
}

void DeviceConfig::SaveToFile(const std::vector<DeviceConfig>& devices) {
    json data;
    data["root"] = json::array();

    // Get master password for encryption
    std::string masterPassword = GlobalConfig::GetActiveMasterPassword();

    for (const auto& dev : devices) {
        json item;
        item["id"] = dev.id;
        item["name"] = dev.name;
        item["username"] = dev.username;
        item["group"] = dev.group;
        item["address"] = dev.address;
        item["port"] = dev.port;
        item["authType"] = dev.auth_method;

        // Encrypt password with master password
        std::string encryptedPassword;
        if (!dev.password.empty() && !masterPassword.empty()) {
            encryptedPassword = encrypt_aes(dev.password, masterPassword);
        }
        item["password"] = encryptedPassword;

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

bool DeviceConfig::ReencryptWithNewPassword(const std::string& oldPassword, const std::string& newPassword) {
    wxString configPath = GetConfigPath();

    std::ifstream file(configPath.ToStdString());
    if (!file.is_open()) {
        return false;
    }

    try {
        json data = json::parse(file);
        file.close();

        // Process each device
        for (auto& item : data["root"]) {
            if (item.contains("password") && !item["password"].is_null()) {
                std::string encrypted_password = item["password"].get<std::string>();

                // Decrypt with old password
                std::string plaintext = decrypt_aes(encrypted_password, oldPassword);
                if (plaintext.empty()) {
                    // If decryption fails, assume it's not encrypted yet
                    plaintext = encrypted_password;
                }

                // Encrypt with new password
                std::string new_encrypted = encrypt_aes(plaintext, newPassword);
                if (new_encrypted.empty()) {
                    return false;
                }

                item["password"] = new_encrypted;
            }
        }

        // Save the re-encrypted data
        std::ofstream outfile(configPath.ToStdString());
        if (!outfile.is_open()) {
            return false;
        }
        outfile << data.dump(2);
        outfile.close();

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}
