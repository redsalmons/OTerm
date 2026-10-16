#ifndef SSHMANAGER_H
#define SSHMANAGER_H

#include <libssh2.h>
#include <string>
#include <fstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#ifndef _WIN32
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

// Logging macros for SSH operations
static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time_t);
#else
    localtime_r(&time_t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S.") << std::setw(3) << std::setfill('0')
        << (std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    return oss.str();
}

extern std::ofstream ssh_log_file;
extern bool ssh_log_initialized;

// Logging enable/disable switch - set to 1 to enable, 0 to disable
#define ENABLE_SSH_LOG 1

#if ENABLE_SSH_LOG
#define SSH_LOG(msg) do { \
    if (ssh_log_initialized) { \
        ssh_log_file << timestamp() << " " << msg << std::endl; \
    } \
} while(0)
#else
#define SSH_LOG(msg) ((void)0)
#endif

#if ENABLE_SSH_LOG
#define SSH_ERR(msg) do { \
    if (ssh_log_initialized) { \
        ssh_log_file << timestamp() << " ERROR: " << msg << std::endl; \
    } \
} while(0)
#else
#define SSH_ERR(msg) ((void)0)
#endif

// Open the per-session SSH log file. Safe to call more than once.
void InitSSHLogFile();

// libssh2 requires one global initialization (WSAStartup on Windows) before
// any session is created. Safe to call from anywhere; runs exactly once.
inline bool EnsureLibssh2Init() {
    static bool ok = (libssh2_init(0) == 0);
    return ok;
}

// True if host is already a numeric dotted-quad IPv4 address, in which case
// no DNS lookup is needed and wxIPV4address::Hostname will not block.
inline bool IsNumericIPv4(const std::string& host) {
#ifdef _WIN32
    IN_ADDR a{};
    return InetPtonA(AF_INET, host.c_str(), &a) == 1;
#else
    in_addr a{};
    return inet_pton(AF_INET, host.c_str(), &a) == 1;
#endif
}

// Blocking DNS resolution to an IPv4 dotted-quad string ("" on failure).
// Must only be called from a worker thread, never the UI thread.
inline std::string ResolveIPv4(const std::string& host) {
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
        return {};
    }
    char buf[INET_ADDRSTRLEN]{};
    auto* sin = reinterpret_cast<sockaddr_in*>(res->ai_addr);
#ifdef _WIN32
    if (!InetNtopA(AF_INET, &sin->sin_addr, buf, sizeof(buf))) {
        buf[0] = '\0';
    }
#else
    if (!inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf))) {
        buf[0] = '\0';
    }
#endif
    freeaddrinfo(res);
    return buf;
}

#endif // SSHMANAGER_H
