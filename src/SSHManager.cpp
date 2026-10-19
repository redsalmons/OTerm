#include "SSHManager.h"
#include "GlobalConfig.h"

// External variables for logging
std::ofstream ssh_log_file;
bool ssh_log_initialized = false;

// Rolling log: ssh.log rotated at 5MB, keeping 7 backups (ssh.log.1 .. .7)
static const long long kMaxLogSize = 5 * 1024 * 1024;
static const int kKeepLogBackups = 7;

static std::filesystem::path LogFilePath() {
    return std::filesystem::path(GlobalConfig::GetLogPath()) / "ssh.log";
}

static void RotateLogIfNeeded() {
    namespace fs = std::filesystem;
    if (!ssh_log_file.is_open() || ssh_log_file.tellp() < kMaxLogSize) {
        return;
    }
    ssh_log_file.close();
    fs::path base = LogFilePath();
    std::error_code ec;
    fs::remove(base.string() + "." + std::to_string(kKeepLogBackups), ec);
    for (int i = kKeepLogBackups - 1; i >= 1; --i) {
        fs::rename(base.string() + "." + std::to_string(i),
                   base.string() + "." + std::to_string(i + 1), ec);
        ec.clear();
    }
    fs::rename(base, base.string() + ".1", ec);
    ssh_log_file.open(base, std::ios::out | std::ios::trunc);
    if (ssh_log_file.is_open()) {
        ssh_log_file << "=== Log rotated ===" << std::endl;
    }
}

void SSHLogWrite(const std::string& line) {
    if (!ssh_log_initialized) return;
    ssh_log_file << timestamp() << " " << line << std::endl;
    ssh_log_file.flush();
    RotateLogIfNeeded();
}

void SSHLogWriteErr(const std::string& line) {
    if (!ssh_log_initialized) return;
    ssh_log_file << timestamp() << " ERROR: " << line << std::endl;
    ssh_log_file.flush();
    RotateLogIfNeeded();
}

void InitSSHLogFile() {
    if (ssh_log_initialized) {
        return;
    }

    try {
        std::filesystem::path log_dir = std::filesystem::path(GlobalConfig::GetLogPath());
        std::filesystem::create_directories(log_dir);

        ssh_log_file.open(LogFilePath(), std::ios::out | std::ios::app);

        if (ssh_log_file.is_open()) {
            ssh_log_initialized = true;
            ssh_log_file << "=== SSH Log Session Started ===" << std::endl;
            ssh_log_file.flush();
        }
    } catch (const std::exception& e) {
    }
}
