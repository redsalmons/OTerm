#include "DeviceConfig.h"
#include "GlobalConfig.h"
#include "SSHManager.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <cstring>
#include <random>

#ifdef __APPLE__
#include <Security/Security.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#pragma comment(lib, "crypt32.lib")
#endif

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
    // Legacy key derivation (MD5) for the old AES-CBC format
    std::string salted = password + std::string((char*)salt, 8);
    return GlobalConfig::ComputeMD5(salted);
}

static std::string derive_key_v2(const std::string& password, const unsigned char* salt, size_t saltLen) {
    // PBKDF2-HMAC-SHA256, 100k iterations -> 32-byte key
    std::string key(32, '\0');
    PKCS5_PBKDF2_HMAC(password.data(), (int)password.size(),
                    salt, (int)saltLen, 100000, EVP_sha256(),
                    32, (unsigned char*)key.data());
    return key;
}

static void fill_random(unsigned char* buf, size_t n) {
    if (RAND_bytes(buf, (int)n) == 1) return;
    std::random_device rd;
    for (size_t i = 0; i < n; i++) {
        buf[i] = static_cast<unsigned char>(rd());
    }
}

static std::string encrypt_aes(const std::string& plaintext, const std::string& password) {
    // v2 format: "v2:" + b64( salt[16] + iv[12] + ciphertext + tag[16] )
    unsigned char salt[16], iv[12], tag[16];
    fill_random(salt, sizeof(salt));
    fill_random(iv, sizeof(iv));

    std::string key = derive_key_v2(password, salt, sizeof(salt));

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return "";

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, (unsigned char*)key.data(), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    std::string ciphertext(plaintext.size(), '\0');
    int len = 0, ciphertext_len = 0;
    if (EVP_EncryptUpdate(ctx, (unsigned char*)ciphertext.data(), &len,
                          (unsigned char*)plaintext.data(), (int)plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    ciphertext_len = len;
    if (EVP_EncryptFinal_ex(ctx, (unsigned char*)ciphertext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    ciphertext_len += len;
    ciphertext.resize(ciphertext_len);

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    EVP_CIPHER_CTX_free(ctx);

    std::string blob;
    blob.append((char*)salt, sizeof(salt));
    blob.append((char*)iv, sizeof(iv));
    blob.append(ciphertext);
    blob.append((char*)tag, sizeof(tag));

    return "v2:" + base64_encode(blob);
}

static std::string decrypt_aes(const std::string& ciphertext_b64, const std::string& password) {
    if (ciphertext_b64.rfind("v2:", 0) == 0) {
        // v2: AES-256-GCM + PBKDF2-HMAC-SHA256
        std::string blob = base64_decode(ciphertext_b64.substr(3));
        const size_t saltLen = 16, ivLen = 12, tagLen = 16;
        if (blob.size() < saltLen + ivLen + tagLen) return "";

        const unsigned char* salt = (unsigned char*)blob.data();
        const unsigned char* iv = salt + saltLen;
        const unsigned char* ct = iv + ivLen;
        size_t ctLen = blob.size() - saltLen - ivLen - tagLen;
        const unsigned char* tag = ct + ctLen;

        std::string key = derive_key_v2(password, salt, saltLen);

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return "";
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
            EVP_DecryptInit_ex(ctx, nullptr, nullptr, (unsigned char*)key.data(), iv) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return "";
        }
        std::string plaintext(ctLen, '\0');
        int len = 0, plaintext_len = 0;
        if (EVP_DecryptUpdate(ctx, (unsigned char*)plaintext.data(), &len, ct, (int)ctLen) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return "";
        }
        plaintext_len = len;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tagLen, (void*)tag) != 1 ||
            EVP_DecryptFinal_ex(ctx, (unsigned char*)plaintext.data() + len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return "";
        }
        plaintext_len += len;
        plaintext.resize(plaintext_len);
        EVP_CIPHER_CTX_free(ctx);
        return plaintext;
    }

    // Legacy v1: AES-256-CBC + MD5 key
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

#ifdef __APPLE__
// ---- macOS Keychain storage for device passwords ----
// Service is the bundle id; account is the device id. When a password lives
// in the Keychain, oc.json stores the literal marker "kc:" instead.

static const char* kKeychainService = "com.oceanterm.app";

static bool KeychainStore(const std::string& account, const std::string& secret) {
    CFStringRef service = CFSTR("com.oceanterm.app");
    CFStringRef acct = CFStringCreateWithCString(nullptr, account.c_str(), kCFStringEncodingUTF8);
    if (!acct) return false;

    CFDataRef data = CFDataCreate(nullptr, (const UInt8*)secret.data(), secret.size());

    const void* queryKeys[] = { kSecClass, kSecAttrService, kSecAttrAccount };
    const void* queryVals[] = { kSecClassGenericPassword, service, acct };
    CFDictionaryRef query = CFDictionaryCreate(nullptr, queryKeys, queryVals, 3,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);

    // Try update first, then add
    const void* attrKeys[] = { kSecValueData };
    const void* attrVals[] = { data };
    CFDictionaryRef attrs = CFDictionaryCreate(nullptr, attrKeys, attrVals, 1,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);

    OSStatus status = SecItemUpdate(query, attrs);
    if (status == errSecItemNotFound) {
        const void* addKeys[] = { kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData };
        const void* addVals[] = { kSecClassGenericPassword, service, acct, data };
        CFDictionaryRef item = CFDictionaryCreate(nullptr, addKeys, addVals, 4,
                                                  &kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);
        status = SecItemAdd(item, nullptr);
        CFRelease(item);
    }

    CFRelease(attrs);
    CFRelease(query);
    CFRelease(data);
    CFRelease(acct);
    return status == errSecSuccess;
}

enum class VaultReadStatus { Ok, NotFound, Error };

static VaultReadStatus KeychainLoad(const std::string& account, std::string& out) {
    CFStringRef service = CFSTR("com.oceanterm.app");
    CFStringRef acct = CFStringCreateWithCString(nullptr, account.c_str(), kCFStringEncodingUTF8);
    if (!acct) return VaultReadStatus::Error;

    const void* keys[] = { kSecClass, kSecAttrService, kSecAttrAccount,
                           kSecReturnData, kSecMatchLimit };
    const void* vals[] = { kSecClassGenericPassword, service, acct,
                           kCFBooleanTrue, kSecMatchLimitOne };
    CFDictionaryRef query = CFDictionaryCreate(nullptr, keys, vals, 5,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);

    CFTypeRef result = nullptr;
    OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    CFRelease(acct);

    if (status == errSecSuccess && result) {
        CFDataRef data = (CFDataRef)result;
        out.assign((const char*)CFDataGetBytePtr(data), CFDataGetLength(data));
        CFRelease(result);
        return VaultReadStatus::Ok;
    }
    if (result) CFRelease(result);
    return status == errSecItemNotFound ? VaultReadStatus::NotFound : VaultReadStatus::Error;
}

// Single "vault" item holding a JSON map of all credentials — one ACL, so
// the system authorization prompt appears at most once per app signature.
static const char* kVaultAccount = "vault";

static VaultReadStatus VaultReadBlob(std::string& out) {
    return KeychainLoad(kVaultAccount, out);
}

static bool VaultWriteBlob(const std::string& blob) {
    return KeychainStore(kVaultAccount, blob);
}
#endif // __APPLE__

#ifdef _WIN32
// ---- Windows DPAPI storage ----
// The vault JSON is encrypted with CryptProtectData (per-user key) and
// stored in vault.bin inside the config directory.
static std::string VaultFilePath() {
    return GetConfigDir().ToStdString() + "/vault.bin";
}

enum class VaultReadStatus { Ok, NotFound, Error };

static VaultReadStatus VaultReadBlob(std::string& out) {
    std::ifstream file(VaultFilePath(), std::ios::binary);
    if (!file.is_open()) return VaultReadStatus::NotFound;
    std::string enc((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (enc.empty()) return VaultReadStatus::NotFound;

    DATA_BLOB inBlob;
    inBlob.pbData = (BYTE*)enc.data();
    inBlob.cbData = (DWORD)enc.size();
    DATA_BLOB outBlob;
    if (!CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, 0, &outBlob)) {
        return VaultReadStatus::Error;
    }
    out.assign((char*)outBlob.pbData, outBlob.cbData);
    LocalFree(outBlob.pbData);
    return VaultReadStatus::Ok;
}

static bool VaultWriteBlob(const std::string& blob) {
    DATA_BLOB inBlob;
    inBlob.pbData = (BYTE*)blob.data();
    inBlob.cbData = (DWORD)blob.size();
    DATA_BLOB outBlob;
    if (!CryptProtectData(&inBlob, nullptr, nullptr, nullptr, nullptr, 0, &outBlob)) {
        return false;
    }
    std::string enc((char*)outBlob.pbData, outBlob.cbData);
    LocalFree(outBlob.pbData);
    return GlobalConfig::WriteFileAtomic(VaultFilePath(), enc);
}
#endif // _WIN32

#if defined(__APPLE__) || defined(_WIN32)
// The vault is read from the backing store once per process and cached, so
// the Keychain is touched (and may prompt) at most once per launch. If the
// read fails for any reason other than "not found", the vault is marked
// unavailable and never written, so an existing vault is never clobbered.
static json s_vaultCache = json::object();
static bool s_vaultLoaded = false;
static bool s_vaultAvailable = false;

static void EnsureVaultLoaded() {
    if (s_vaultLoaded) return;
    std::string blob;
    VaultReadStatus status = VaultReadBlob(blob);
    s_vaultLoaded = true;
    s_vaultAvailable = (status != VaultReadStatus::Error);
    if (status == VaultReadStatus::Ok && !blob.empty()) {
        try {
            s_vaultCache = json::parse(blob);
            if (!s_vaultCache.is_object()) s_vaultAvailable = false;
        } catch (...) {
            s_vaultAvailable = false;
        }
    }
}

static json LoadVault() {
    EnsureVaultLoaded();
    return s_vaultAvailable ? s_vaultCache : json::object();
}

static bool StoreVault(const json& vault) {
    EnsureVaultLoaded();
    if (!s_vaultAvailable) return false;
    if (!VaultWriteBlob(vault.dump())) return false;
    s_vaultCache = vault;
    return true;
}
#endif

// Marker written to oc.json when the password lives in the vault
// (Keychain on macOS / DPAPI-encrypted file on Windows)
static const char* kKeychainMarker = "kc:";

// Encode a device password for storage in oc.json. On macOS the Keychain is
// preferred; elsewhere (or if Keychain fails) we encrypt with the master
// password using the v2 AES-256-GCM format.
static std::string EncodeStoredPassword(const std::string& deviceId,
                                        const std::string& password,
                                        const std::string& masterPassword) {
#if defined(__APPLE__) || defined(_WIN32)
    EnsureVaultLoaded();
    if (!s_vaultAvailable) {
        // Vault unreadable this session: keep pointing at it rather than
        // dropping a password that may still be stored there.
        return password.empty() ? "" : kKeychainMarker;
    }
    if (!password.empty()) {
        json vault = LoadVault();
        vault[deviceId] = password;
        if (StoreVault(vault)) {
            return kKeychainMarker;
        }
    } else {
        // No password: drop any stale entry from the vault
        json vault = LoadVault();
        if (vault.erase(deviceId) > 0) {
            StoreVault(vault);
        }
        return "";
    }
#endif
    if (!password.empty() && !masterPassword.empty()) {
        return encrypt_aes(password, masterPassword);
    }
    return "";
}

static std::string DecodeStoredPassword(const std::string& stored,
                                        const std::string& deviceId,
                                        const std::string& masterPassword) {
#if defined(__APPLE__) || defined(_WIN32)
    if (stored == kKeychainMarker) {
        json vault = LoadVault();
        return vault.value(deviceId, "");
    }
#endif
    if (!stored.empty() && !masterPassword.empty()) {
        return decrypt_aes(stored, masterPassword);
    }
    return stored;
}

std::vector<DeviceConfig> DeviceConfig::LoadFromFile() {
    GlobalConfig::EnsureConfigMigration();
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
        bool migratedToVault = false;

        for (const auto& item : data["root"]) {
            DeviceConfig dev;
            dev.id = item.value("id", "");
            dev.name = item.value("name", "");
            dev.group = item.value("group", "");
            std::string encryptedPassword;
            
            // Handle both nested "config" structure and flat structure
            if (item.contains("config") && item["config"].is_object()) {
                auto& config = item["config"];
                dev.username = config.value("username", "");
                dev.address = config.value("address", "");
                dev.port = config.value("port", "22");
                dev.auth_method = config.value("authMethod", "password");
                encryptedPassword = config.value("password", "");
                dev.password = DecodeStoredPassword(encryptedPassword, dev.id, masterPassword);
            } else {
                // Flat structure (fallback)
                dev.username = item.value("username", "");
                dev.address = item.value("address", "");
                dev.port = item.value("port", "22");
                dev.auth_method = item.value("authType", "password");
                encryptedPassword = item.value("password", "");
                dev.password = DecodeStoredPassword(encryptedPassword, dev.id, masterPassword);
            }

#if defined(__APPLE__) || defined(_WIN32)
            if (!encryptedPassword.empty() && encryptedPassword != kKeychainMarker && !dev.password.empty()) {
                migratedToVault = true;
            }
#endif

            devices.push_back(dev);
        }

#if defined(__APPLE__) || defined(_WIN32)
        if (migratedToVault && IsCredentialStoreAvailable()) {
            SaveToFile(devices);
        }
#endif
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

        item["password"] = EncodeStoredPassword(dev.id, dev.password, masterPassword);

        data["root"].push_back(item);
    }

    wxString configDir = GetConfigDir();
    wxString configPath = GetConfigPath();

    if (!wxDirExists(configDir)) {
        wxFileName::Mkdir(configDir);
    }

    GlobalConfig::WriteFileAtomic(configPath.ToStdString(), data.dump(2));
}

bool DeviceConfig::SaveLoginCredentials(const std::string& deviceId,
                                        const std::string& username,
                                        const std::string& password) {
    if (deviceId.empty() || password.empty()) return false;
    std::vector<DeviceConfig> devices = LoadFromFile();
    auto it = std::find_if(devices.begin(), devices.end(),
        [&deviceId](const DeviceConfig& d) { return d.id == deviceId; });
    if (it == devices.end()) return false;

    bool changed = false;
    if (it->password != password) {
        it->password = password;
        changed = true;
    }
    if (it->username.empty() && !username.empty()) {
        it->username = username;
        changed = true;
    }
    if (changed) SaveToFile(devices);
    return true;
}

bool DeviceConfig::StoreCredential(const std::string& key, const std::string& secret) {
#if defined(__APPLE__) || defined(_WIN32)
    json vault = LoadVault();
    vault[key] = secret;
    return StoreVault(vault);
#else
    (void)key; (void)secret;
    return false;
#endif
}

bool DeviceConfig::IsCredentialStoreAvailable() {
#if defined(__APPLE__) || defined(_WIN32)
    EnsureVaultLoaded();
    return s_vaultAvailable;
#else
    return false;
#endif
}

std::string DeviceConfig::LoadCredential(const std::string& key) {
#if defined(__APPLE__) || defined(_WIN32)
    json vault = LoadVault();
    return vault.value(key, "");
#else
    (void)key;
    return "";
#endif
}

void DeviceConfig::DeleteCredential(const std::string& deviceId) {
#if defined(__APPLE__) || defined(_WIN32)
    json vault = LoadVault();
    if (vault.erase(deviceId) > 0) {
        StoreVault(vault);
    }
#else
    (void)deviceId;
#endif
}
