#include "SSHManager.h"
#include "GlobalConfig.h"

// External variables for logging
std::ofstream ssh_log_file;
bool ssh_log_initialized = false;

void InitSSHLogFile() {
#if ENABLE_SSH_LOG
    if (ssh_log_initialized) {
        return;
    }

    try {
        std::filesystem::path log_dir = std::filesystem::path(GlobalConfig::GetLogPath());
        std::filesystem::create_directories(log_dir);

        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream filename;
        filename << "ssh_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".log";

        std::filesystem::path log_path = log_dir / filename.str();
        ssh_log_file.open(log_path, std::ios::out | std::ios::app);

        if (ssh_log_file.is_open()) {
            ssh_log_initialized = true;
            ssh_log_file << "=== SSH Log Session Started ===" << std::endl;
            ssh_log_file.flush();
        }
    } catch (const std::exception& e) {
    }
#endif
}
